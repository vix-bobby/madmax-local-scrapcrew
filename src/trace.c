/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Wrappers for the functions the game imports: log the call, pass it on, log the result.
 * src/winhttp.def exports hk_Name as Name. Only calls for the game's two online hosts are
 * changed: they go to the listener inside this DLL, without TLS and without any proxy.
 *
 * Request and response bodies are never logged, nor are query strings, credentials or cookies.
 *
 * In asynchronous mode a call can complete on another thread before it returns, so arguments are
 * read before the real function is called, and output buffers of the calls that complete through
 * a callback are not read at all.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>

#include "proxy.h"
#include "redirect.h"
#include "server.h"

#define REAL(name) ((__typeof__(&name))real_export(EXPORT_##name))
#define TEXT_SIZE 1024

/* log the outcome of a call that returns BOOL or a handle */
#define LOG_RESULT(name, handle, ok)                                                             \
    do {                                                                                         \
        if (ok)                                                                                  \
            log_line("  %s(%p) ok", name, (void *)(handle));                                     \
        else                                                                                     \
            log_line("  %s(%p) failed, error %lu", name, (void *)(handle), GetLastError());      \
    } while (0)

/* ---------------------------------------------------------------- text */

/* UTF-16 text of `length` characters (-1: up to the terminator) as UTF-8, cut to fit. */
static const char *text(char *out, int size, const wchar_t *wide, DWORD length)
{
    DWORD error = GetLastError();
    int n = 0;

    if (!wide) {
        strcpy(out, "(null)");
        return out;
    }
    if (length == (DWORD)-1)
        length = (DWORD)wcslen(wide);
    if (length > (DWORD)(size - 1) / 3)
        length = (DWORD)(size - 1) / 3;
    if (length)
        n = WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, out, size - 1, NULL, NULL);
    out[n] = '\0';
    SetLastError(error);
    return out;
}

/* a path or URL without its query string */
static const char *no_query(char *out, int size, const wchar_t *wide, DWORD length)
{
    char *query = strchr(text(out, size, wide, length), '?');

    if (query)
        strcpy(query, strlen(query) >= 4 ? "?..." : "?");
    return out;
}

/* one log line per header line, with secrets left out */
static void log_headers(const char *what, const wchar_t *headers, DWORD length)
{
    static const char *const secret[] = { "authorization", "proxy-authorization", "cookie",
                                          "set-cookie" };
    char all[TEXT_SIZE * 2];
    char *line, *end;

    if (!headers || !length)
        return;
    text(all, sizeof all, headers, length);
    for (line = all; *line; line = end) {
        end = line + strcspn(line, "\r\n");
        if (*end) {
            *end++ = '\0';
            end += strspn(end, "\r\n");
        }
        size_t name = strcspn(line, ":");
        for (size_t i = 0; i < sizeof secret / sizeof *secret; i++)
            if (line[name] && strlen(secret[i]) == name && !_strnicmp(line, secret[i], name)) {
                strcpy(line + name, ": ...");
                break;
            }
        if (*line)
            log_line("    %s: %s", what, line);
    }
}

/* the module holding `address`: tells the game's sessions from other ones */
static const char *module_of(char *out, int size, void *address)
{
    wchar_t path[MAX_PATH * 4];
    HMODULE module;
    DWORD n = 0;

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           address, &module)) {
        const char *role = winhttp_role(module);
        if (role) {
            snprintf(out, (size_t)size, "%s winhttp.dll", role);
            return out;
        }
        n = GetModuleFileNameW(module, path, MAX_PATH * 4);
    }
    if (!n || n >= MAX_PATH * 4)
        return "?";
    const wchar_t *name = wcsrchr(path, L'\\');
    return text(out, size, name ? name + 1 : path, (DWORD)-1);
}

/* ---------------------------------------------------------------- status callback */

static const char *status_name(DWORD status)
{
    switch (status) {
#define S(name) case WINHTTP_CALLBACK_STATUS_##name: return #name;
        S(RESOLVING_NAME) S(NAME_RESOLVED) S(CONNECTING_TO_SERVER) S(CONNECTED_TO_SERVER)
        S(SENDING_REQUEST) S(REQUEST_SENT) S(RECEIVING_RESPONSE) S(RESPONSE_RECEIVED)
        S(CLOSING_CONNECTION) S(CONNECTION_CLOSED) S(HANDLE_CREATED) S(HANDLE_CLOSING)
        S(DETECTING_PROXY) S(REDIRECT) S(INTERMEDIATE_RESPONSE) S(SECURE_FAILURE)
        S(HEADERS_AVAILABLE) S(DATA_AVAILABLE) S(READ_COMPLETE) S(WRITE_COMPLETE)
        S(REQUEST_ERROR) S(SENDREQUEST_COMPLETE)
#undef S
    }
    return "?";
}

static void log_status(HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID info,
                       DWORD length)
{
    char detail[TEXT_SIZE] = "";

    switch (status) {
    case WINHTTP_CALLBACK_STATUS_RESOLVING_NAME:
    case WINHTTP_CALLBACK_STATUS_NAME_RESOLVED:
    case WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER:
    case WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER:
    case WINHTTP_CALLBACK_STATUS_REDIRECT:
        if (info)
            no_query(detail, sizeof detail, info, (DWORD)wcsnlen(info, length));
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        if (info && length >= sizeof(WINHTTP_ASYNC_RESULT)) {
            const WINHTTP_ASYNC_RESULT *result = info;
            snprintf(detail, sizeof detail, "api %lu, error %lu",
                     (unsigned long)result->dwResult, result->dwError);
        }
        break;
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
    case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
    case WINHTTP_CALLBACK_STATUS_REQUEST_SENT:
    case WINHTTP_CALLBACK_STATUS_RESPONSE_RECEIVED:
    case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
        if (info && length >= sizeof(DWORD))
            snprintf(detail, sizeof detail, "value %lu (0x%lx)", *(DWORD *)info, *(DWORD *)info);
        break;
    }
    log_line("callback %p context %llx: %s (0x%08lx) length %lu %s", handle,
             (unsigned long long)context, status_name(status), status, length, detail);
}

/* WinHTTP passes a callback nothing that identifies it, so each of the caller's callbacks gets
 * its own trampoline. A handle inherits its parent's trampoline with no bookkeeping here. */
#define CALLBACKS 8
static WINHTTP_STATUS_CALLBACK original[CALLBACKS];

#define TRAMPOLINE(i)                                                                            \
    static void CALLBACK trampoline_##i(HINTERNET handle, DWORD_PTR context, DWORD status,       \
                                        LPVOID info, DWORD length)                               \
    {                                                                                            \
        WINHTTP_STATUS_CALLBACK next = original[i];                                              \
        log_status(handle, context, status, info, length);                                       \
        next(handle, context, status, info, length);                                             \
    }
TRAMPOLINE(0) TRAMPOLINE(1) TRAMPOLINE(2) TRAMPOLINE(3)
TRAMPOLINE(4) TRAMPOLINE(5) TRAMPOLINE(6) TRAMPOLINE(7)
static const WINHTTP_STATUS_CALLBACK trampoline[CALLBACKS] = {
    trampoline_0, trampoline_1, trampoline_2, trampoline_3,
    trampoline_4, trampoline_5, trampoline_6, trampoline_7,
};

static WINHTTP_STATUS_CALLBACK wrap_callback(WINHTTP_STATUS_CALLBACK callback)
{
    if (!callback || callback == WINHTTP_INVALID_STATUS_CALLBACK || !stays_loaded())
        return callback;
    for (int i = 0; i < CALLBACKS; i++) {
        if (callback == trampoline[i])
            return callback;
        WINHTTP_STATUS_CALLBACK seen = InterlockedCompareExchangePointer(
            (void **)&original[i], (void *)callback, NULL);
        if (!seen || seen == callback)
            return trampoline[i];
    }
    log_line("too many different status callbacks: %p is not traced", (void *)callback);
    return callback;
}

static WINHTTP_STATUS_CALLBACK unwrap_callback(WINHTTP_STATUS_CALLBACK callback)
{
    for (int i = 0; i < CALLBACKS; i++)
        if (callback == trampoline[i])
            return original[i];
    return callback;
}

/* ---------------------------------------------------------------- wrappers */

BOOL WINAPI hk_WinHttpCheckPlatform(void)
{
    BOOL ok = REAL(WinHttpCheckPlatform)();

    LOG_RESULT("WinHttpCheckPlatform", NULL, ok);
    return ok;
}

HINTERNET WINAPI hk_WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass,
                                DWORD flags)
{
    char a[TEXT_SIZE], m[TEXT_SIZE];

    log_line("WinHttpOpen agent \"%s\" access %lu proxy %s flags 0x%lx%s from %s",
             text(a, sizeof a, agent, (DWORD)-1), access, proxy ? "set" : "none", flags,
             flags & WINHTTP_FLAG_ASYNC ? " (ASYNC)" : "",
             module_of(m, sizeof m, __builtin_return_address(0)));
    HINTERNET session = REAL(WinHttpOpen)(agent, access, proxy, bypass, flags);
    redirect_forget(session);
    LOG_RESULT("WinHttpOpen", session, session);
    return session;
}

HINTERNET WINAPI hk_WinHttpConnect(HINTERNET session, LPCWSTR server, INTERNET_PORT port,
                                   DWORD reserved)
{
    char s[TEXT_SIZE];

    log_line("WinHttpConnect(%p) \"%s\" port %u", session, text(s, sizeof s, server, (DWORD)-1),
             (unsigned)port);
    unsigned short local = redirect_host(server) ? server_port() : 0;
    HINTERNET connection = REAL(WinHttpConnect)(session, local ? L"127.0.0.1" : server,
                                                local ? local : port, reserved);
    LOG_RESULT("WinHttpConnect", connection, connection);
    redirect_forget(connection);
    if (local && connection) {
        if (redirect_remember(connection)) {
            log_line("  redirected to the listener");
        } else { /* no room to keep track of it: leave it as the caller asked */
            log_line("  too many connections: NOT redirected");
            REAL(WinHttpCloseHandle)(connection);
            connection = REAL(WinHttpConnect)(session, server, port, reserved);
        }
    }
    return connection;
}

HINTERNET WINAPI hk_WinHttpOpenRequest(HINTERNET connection, LPCWSTR verb, LPCWSTR path,
                                       LPCWSTR version, LPCWSTR referrer, LPCWSTR *accept,
                                       DWORD flags)
{
    char v[64], p[TEXT_SIZE], ver[64];

    log_line("WinHttpOpenRequest(%p) %s \"%s\" version %s flags 0x%lx%s", connection,
             text(v, sizeof v, verb, (DWORD)-1), no_query(p, sizeof p, path, (DWORD)-1),
             text(ver, sizeof ver, version, (DWORD)-1), flags,
             flags & WINHTTP_FLAG_SECURE ? " (SECURE)" : "");
    int local = redirected(connection);
    HINTERNET request = REAL(WinHttpOpenRequest)(
        connection, verb, path, version, referrer, accept,
        local ? flags & ~(DWORD)WINHTTP_FLAG_SECURE : flags);
    LOG_RESULT("WinHttpOpenRequest", request, request);
    redirect_forget(request);
    if (local && request) {
        /* a configured proxy must not come between the game and the listener */
        WINHTTP_PROXY_INFO direct = { WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL };
        if (!REAL(WinHttpSetOption)(request, WINHTTP_OPTION_PROXY, &direct, sizeof direct))
            log_line("  cannot turn the proxy off, error %lu", GetLastError());
    }
    return request;
}

BOOL WINAPI hk_WinHttpAddRequestHeaders(HINTERNET request, LPCWSTR headers, DWORD length,
                                        DWORD modifiers)
{
    log_line("WinHttpAddRequestHeaders(%p) modifiers 0x%lx", request, modifiers);
    log_headers("added", headers, length);
    BOOL ok = REAL(WinHttpAddRequestHeaders)(request, headers, length, modifiers);
    LOG_RESULT("WinHttpAddRequestHeaders", request, ok);
    return ok;
}

/* Options whose value is a number or a context. Others can be strings, whose length is counted
 * in characters, so a short password would pass for a number. */
static int numeric_option(DWORD option)
{
    switch (option) {
    case WINHTTP_OPTION_RESOLVE_TIMEOUT:
    case WINHTTP_OPTION_CONNECT_TIMEOUT:
    case WINHTTP_OPTION_CONNECT_RETRIES:
    case WINHTTP_OPTION_SEND_TIMEOUT:
    case WINHTTP_OPTION_RECEIVE_TIMEOUT:
    case WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT:
    case WINHTTP_OPTION_SECURITY_FLAGS:
    case WINHTTP_OPTION_CONTEXT_VALUE:
    case WINHTTP_OPTION_DISABLE_FEATURE:
    case WINHTTP_OPTION_MAX_CONNS_PER_SERVER:
    case WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER:
    case WINHTTP_OPTION_AUTOLOGON_POLICY:
    case WINHTTP_OPTION_ENABLE_FEATURE:
    case WINHTTP_OPTION_SECURE_PROTOCOLS:
    case WINHTTP_OPTION_REDIRECT_POLICY:
        return 1;
    }
    return 0;
}

BOOL WINAPI hk_WinHttpSetOption(HINTERNET handle, DWORD option, LPVOID buffer, DWORD length)
{
    unsigned long long value = 0;

    if (numeric_option(option) && buffer
        && (length == sizeof(DWORD) || length == sizeof value)) {
        memcpy(&value, buffer, length);
        log_line("WinHttpSetOption(%p) option %lu length %lu value 0x%llx", handle, option,
                 length, value);
    } else {
        log_line("WinHttpSetOption(%p) option %lu length %lu", handle, option, length);
    }
    BOOL ok = REAL(WinHttpSetOption)(handle, option, buffer, length);
    LOG_RESULT("WinHttpSetOption", handle, ok);
    return ok;
}

BOOL WINAPI hk_WinHttpSetTimeouts(HINTERNET handle, int resolve, int connect, int send,
                                  int receive)
{
    log_line("WinHttpSetTimeouts(%p) resolve %d connect %d send %d receive %d", handle, resolve,
             connect, send, receive);
    BOOL ok = REAL(WinHttpSetTimeouts)(handle, resolve, connect, send, receive);
    LOG_RESULT("WinHttpSetTimeouts", handle, ok);
    return ok;
}

WINHTTP_STATUS_CALLBACK WINAPI hk_WinHttpSetStatusCallback(HINTERNET handle,
                                                           WINHTTP_STATUS_CALLBACK callback,
                                                           DWORD flags, DWORD_PTR reserved)
{
    log_line("WinHttpSetStatusCallback(%p) callback %p mask 0x%08lx", handle, (void *)callback,
             flags);
    WINHTTP_STATUS_CALLBACK previous = REAL(WinHttpSetStatusCallback)(
        handle, wrap_callback(callback), flags, reserved);
    LOG_RESULT("WinHttpSetStatusCallback", handle, previous != WINHTTP_INVALID_STATUS_CALLBACK);
    return unwrap_callback(previous);
}

BOOL WINAPI hk_WinHttpSendRequest(HINTERNET request, LPCWSTR headers, DWORD headers_length,
                                  LPVOID optional, DWORD optional_length, DWORD total_length,
                                  DWORD_PTR context)
{
    log_line("WinHttpSendRequest(%p) body %lu of %lu bytes, context %llx", request,
             optional_length, total_length, (unsigned long long)context);
    log_headers("sent", headers, headers_length);
    BOOL ok = REAL(WinHttpSendRequest)(request, headers, headers_length, optional,
                                       optional_length, total_length, context);
    LOG_RESULT("WinHttpSendRequest", request, ok);
    return ok;
}

BOOL WINAPI hk_WinHttpReceiveResponse(HINTERNET request, LPVOID reserved)
{
    log_line("WinHttpReceiveResponse(%p)", request);
    BOOL ok = REAL(WinHttpReceiveResponse)(request, reserved);
    LOG_RESULT("WinHttpReceiveResponse", request, ok);
    return ok;
}

/* synchronous even on an asynchronous session, so its outputs can be read afterwards */
BOOL WINAPI hk_WinHttpQueryHeaders(HINTERNET request, DWORD level, LPCWSTR name, LPVOID buffer,
                                   LPDWORD length, LPDWORD index)
{
    char n[256], value[TEXT_SIZE];
    DWORD which = level & 0xffff;

    /* `name` is a string only for a custom header */
    log_line("WinHttpQueryHeaders(%p) level 0x%08lx%s%s buffer %s size %lu", request, level,
             which == WINHTTP_QUERY_CUSTOM ? " name " : "",
             which == WINHTTP_QUERY_CUSTOM ? text(n, sizeof n, name, (DWORD)-1) : "",
             buffer ? "given" : "none", length ? *length : 0);
    BOOL ok = REAL(WinHttpQueryHeaders)(request, level, name, buffer, length, index);
    LOG_RESULT("WinHttpQueryHeaders", request, ok);
    if (!ok || !buffer || !length)
        return ok;
    if (level & WINHTTP_QUERY_FLAG_NUMBER)
        log_line("    value %lu", *(DWORD *)buffer);
    else if (level & WINHTTP_QUERY_FLAG_SYSTEMTIME)
        log_line("    value is a time");
    else if (which == WINHTTP_QUERY_RAW_HEADERS) /* terminator-separated list */
        log_line("    value is the raw header list, %lu bytes", *length);
    else if (which == WINHTTP_QUERY_SET_COOKIE || which == WINHTTP_QUERY_COOKIE)
        log_line("    value is a cookie");
    else if (which == WINHTTP_QUERY_RAW_HEADERS_CRLF)
        log_headers("value", buffer, *length / sizeof(wchar_t));
    else
        log_line("    value \"%s\"", text(value, sizeof value, buffer, *length / sizeof(wchar_t)));
    return ok;
}

BOOL WINAPI hk_WinHttpQueryDataAvailable(HINTERNET request, LPDWORD available)
{
    log_line("WinHttpQueryDataAvailable(%p)%s", request, available ? " with result pointer" : "");
    BOOL ok = REAL(WinHttpQueryDataAvailable)(request, available);
    LOG_RESULT("WinHttpQueryDataAvailable", request, ok);
    return ok;
}

BOOL WINAPI hk_WinHttpReadData(HINTERNET request, LPVOID buffer, DWORD size, LPDWORD read)
{
    log_line("WinHttpReadData(%p) size %lu%s", request, size, read ? " with result pointer" : "");
    BOOL ok = REAL(WinHttpReadData)(request, buffer, size, read);
    LOG_RESULT("WinHttpReadData", request, ok);
    return ok;
}

BOOL WINAPI hk_WinHttpCloseHandle(HINTERNET handle)
{
    log_line("WinHttpCloseHandle(%p)", handle);
    redirect_forget(handle); /* before the value can be reused */
    BOOL ok = REAL(WinHttpCloseHandle)(handle);
    LOG_RESULT("WinHttpCloseHandle", handle, ok);
    return ok;
}
