/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * A small HTTP listener inside the game process, on 127.0.0.1 only, that answers the game's
 * redirected requests: one request per connection, one thread per connection. It also keeps the
 * state file next to the log.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proxy.h"
#include "responder.h"
#include "server.h"

#define STATE_NAME L"madmax_scrapcrew.dat"
#define HEAD_LIMIT 8192
#define BODY_LIMIT (256 * 1024)
#define TIMEOUT_MS 10000

static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static unsigned short port;
static CRITICAL_SECTION state_lock;
static struct state state;
static wchar_t state_path[PATH_CHARS], new_path[PATH_CHARS];

/* ---------------------------------------------------------------- state file */

static void load_state(void)
{
    char text[STATE_TEXT_SIZE];
    FILE *f = _wfopen(state_path, L"rb");

    if (!f) {
        log_line("no saved state: the next reward counts as the first");
        return;
    }
    text[fread(text, 1, sizeof text - 1, f)] = '\0';
    fclose(f);
    state_read(&state, text);
    log_line("saved state read: profile %s, last reward %llu", state.account[0] ? "yes" : "no",
             state.scrap_rewarded);
}

/* written beside the old file, then moved over it */
static void save_state(void)
{
    char text[STATE_TEXT_SIZE];
    size_t length = state_write(&state, text, sizeof text);
    FILE *f = length ? _wfopen(new_path, L"wb") : NULL;
    int ok = f && fwrite(text, 1, length, f) == length;

    if (f)
        ok = !fclose(f) && ok;
    if (ok)
        ok = MoveFileExW(new_path, state_path,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
             != 0;
    if (!ok)
        log_line("CANNOT SAVE THE STATE FILE (error %lu)", GetLastError());
}

/* ---------------------------------------------------------------- connections */

/* the value of a header in a request head, cut to fit; "" if absent */
static void header(const char *head, const char *name, char *out, size_t size)
{
    size_t n = strlen(name);

    out[0] = '\0';
    for (const char *line = strstr(head, "\r\n"); line; line = strstr(line, "\r\n")) {
        line += 2;
        if (_strnicmp(line, name, n) || line[n] != ':')
            continue;
        line += n + 1;
        line += strspn(line, " \t");
        size_t length = strcspn(line, "\r\n");
        if (length >= size)
            length = size - 1;
        memcpy(out, line, length);
        out[length] = '\0';
        return;
    }
}

static int send_all(SOCKET s, const void *data, size_t length)
{
    const char *at = data;

    while (length) {
        int n = send(s, at, (int)length, 0);
        if (n <= 0)
            return 0;
        at += n;
        length -= (size_t)n;
    }
    return 1;
}

static void answer(SOCKET s, const struct reply *reply)
{
    char head[256];
    int n = snprintf(head, sizeof head,
                     "HTTP/1.1 %d %s\r\n"
                     "Accept: %s\r\n"
                     "Content-Type: %s\r\n"
                     "X-Hydra-Error-Code: %d\r\n"
                     "X-Hydra-Processing-Time: 1\r\n"
                     "Content-Length: %u\r\n"
                     "Connection: close\r\n\r\n",
                     reply->status, reply->status == 200 ? "OK" : "Error", reply->content_type,
                     reply->content_type, reply->status, (unsigned)reply->length);

    if (send_all(s, head, (size_t)n))
        send_all(s, reply->body, reply->length);
}

static void serve(SOCKET s)
{
    char head[HEAD_LIMIT], method[8], path[512], action[256], size_text[24];
    char *body = NULL, *end = NULL;
    size_t have = 0, length, got;
    int n;

    while (!end) {
        if (have == sizeof head - 1 || (n = recv(s, head + have, (int)(sizeof head - 1 - have), 0)) <= 0)
            return;
        have += (size_t)n;
        head[have] = '\0';
        end = strstr(head, "\r\n\r\n");
    }
    end += 4;
    got = have - (size_t)(end - head);
    end[-2] = '\0'; /* the head ends here; the first body bytes stay where they are */
    if (sscanf(head, "%7s %511s", method, path) != 2)
        return;
    header(head, "SOAPAction", action, sizeof action);
    header(head, "Content-Length", size_text, sizeof size_text);
    length = strtoul(size_text, NULL, 10);
    if (length > BODY_LIMIT || got > length || !(body = malloc(length + 1))) {
        log_line("listener: %s %s dropped, body of %s bytes", method, path, size_text);
        return;
    }
    memcpy(body, end, got);
    while (got < length) {
        if ((n = recv(s, body + got, (int)(length - got), 0)) <= 0) {
            free(body);
            return;
        }
        got += (size_t)n;
    }

    struct request request = { method, path, action, (const unsigned char *)body, length };
    struct reply reply;

    EnterCriticalSection(&state_lock);
    unsigned long long rewarded = state.scrap_rewarded;
    if (respond(&state, &request, time(NULL), &reply))
        save_state();
    if (state.scrap_rewarded != rewarded)
        log_line("listener: reward time recorded: %llu", state.scrap_rewarded);
    LeaveCriticalSection(&state_lock);
    free(body);

    log_line("listener: %s %s, %u bytes -> %d, %u bytes", method, path, (unsigned)length,
             reply.status, (unsigned)reply.length);
    answer(s, &reply);
}

static DWORD WINAPI connection(void *parameter)
{
    SOCKET s = (SOCKET)(UINT_PTR)parameter;
    DWORD timeout = TIMEOUT_MS;
    char rest[512];

    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout);
    serve(s);
    /* let the client close first, so that nothing it has not read yet is thrown away */
    shutdown(s, SD_SEND);
    while (recv(s, rest, sizeof rest, 0) > 0) {
    }
    closesocket(s);
    return 0;
}

static DWORD WINAPI accept_loop(void *parameter)
{
    SOCKET listener = (SOCKET)(UINT_PTR)parameter;

    for (;;) {
        SOCKET s = accept(listener, NULL, NULL);
        if (s == INVALID_SOCKET) {
            Sleep(100);
            continue;
        }
        HANDLE thread = CreateThread(NULL, 0, connection, (void *)(UINT_PTR)s, 0, NULL);
        if (thread)
            CloseHandle(thread);
        else
            closesocket(s);
    }
    return 0; /* not reached */
}

/* ---------------------------------------------------------------- start */

static BOOL CALLBACK start(PINIT_ONCE o, PVOID p, PVOID *c)
{
    struct sockaddr_in address = { 0 };
    int size = sizeof address;
    BOOL exclusive = TRUE;
    WSADATA wsa;
    SOCKET s;
    HANDLE thread;

    (void)o, (void)p, (void)c;
    /* the threads below run our code for as long as the process lives */
    if (!stays_loaded() || !mod_file(state_path, STATE_NAME)
        || !mod_file(new_path, STATE_NAME L".new")) {
        log_line("listener not started");
        return TRUE;
    }
    InitializeCriticalSection(&state_lock);
    load_state();

    if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
        log_line("listener not started: no Winsock");
        return TRUE;
    }
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* never reachable from elsewhere */
    if (s == INVALID_SOCKET
        || setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive,
                      sizeof exclusive)
        || bind(s, (struct sockaddr *)&address, sizeof address) || listen(s, 16)
        || getsockname(s, (struct sockaddr *)&address, &size)
        || !(thread = CreateThread(NULL, 0, accept_loop, (void *)(UINT_PTR)s, 0, NULL))) {
        log_line("listener not started: error %d", WSAGetLastError());
        if (s != INVALID_SOCKET)
            closesocket(s);
        return TRUE;
    }
    CloseHandle(thread);
    port = ntohs(address.sin_port);
    log_line("listener on 127.0.0.1 port %u", (unsigned)port);
    return TRUE;
}

unsigned short server_port(void)
{
    DWORD error = GetLastError();

    InitOnceExecuteOnce(&once, start, NULL, NULL);
    SetLastError(error);
    return port;
}
