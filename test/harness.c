/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Test program for the proxy. It imports winhttp.dll statically, like the game, so it must run
 * from a folder that holds the proxy: Windows then binds its imports to the proxy at load time.
 * The server it talks to is a thread of its own on 127.0.0.1; nothing leaves the machine.
 */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(what)                                                                              \
    do {                                                                                         \
        if (!(what)) {                                                                           \
            printf("FAIL line %d: %s (error %lu)\n", __LINE__, #what, GetLastError());           \
            failures++;                                                                          \
        }                                                                                        \
    } while (0)

/* ---------------------------------------------------------------- server */

static const char BODY[] = "\x0e\x00\x00\x00\x04" "0801";
#define BODY_SIZE 9

static DWORD WINAPI serve(void *listener)
{
    for (;;) {
        SOCKET c = accept((SOCKET)(UINT_PTR)listener, NULL, NULL);
        char request[4096];
        int have = 0, n;

        if (c == INVALID_SOCKET)
            return 0;
        /* every test request ends with its body, "ping" */
        while (have < (int)sizeof request - 1
               && (n = recv(c, request + have, sizeof request - 1 - have, 0)) > 0) {
            have += n;
            request[have] = '\0';
            if (strstr(request, "\r\n\r\nping"))
                break;
        }
        char reply[512];
        int head = snprintf(reply, sizeof reply,
                            "HTTP/1.1 200 OK\r\nContent-Type: application/x-hydra-binary\r\n"
                            "X-Hydra-Error-Code: 200\r\nSet-Cookie: s=SECRETCOOKIE\r\n"
                            "Content-Length: %d\r\nConnection: close\r\n\r\n",
                            BODY_SIZE);
        memcpy(reply + head, BODY, BODY_SIZE);
        send(c, reply, head + BODY_SIZE, 0);
        closesocket(c);
    }
}

static INTERNET_PORT start_server(void)
{
    WSADATA wsa;
    struct sockaddr_in address = { 0 };
    int size = sizeof address;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (struct sockaddr *)&address, sizeof address) || listen(s, 8)
        || getsockname(s, (struct sockaddr *)&address, &size))
        return 0;
    CreateThread(NULL, 0, serve, (void *)(UINT_PTR)s, 0, NULL);
    return ntohs(address.sin_port);
}

/* ---------------------------------------------------------------- forwarding */

static void *export_by_ordinal(HMODULE module, DWORD ordinal, const char **name)
{
    const BYTE *base = (const BYTE *)module;
    const IMAGE_NT_HEADERS *nt = (const void *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    const IMAGE_EXPORT_DIRECTORY *e =
        (const void *)(base + nt->OptionalHeader.DataDirectory[0].VirtualAddress);
    const DWORD *functions = (const void *)(base + e->AddressOfFunctions);
    const DWORD *names = (const void *)(base + e->AddressOfNames);
    const WORD *ordinals = (const void *)(base + e->AddressOfNameOrdinals);

    *name = NULL;
    if (ordinal < e->Base || ordinal - e->Base >= e->NumberOfFunctions)
        return NULL;
    for (DWORD i = 0; i < e->NumberOfNames; i++)
        if (ordinals[i] == ordinal - e->Base)
            *name = (const char *)base + names[i];
    return functions[ordinal - e->Base] ? (void *)(base + functions[ordinal - e->Base]) : NULL;
}

static void test_exports(HMODULE proxy, HMODULE real)
{
    int count = 0;

    for (DWORD ordinal = 1; ordinal < 512; ordinal++) {
        const char *ours, *theirs;
        void *a = export_by_ordinal(proxy, ordinal, &ours);
        void *b = export_by_ordinal(real, ordinal, &theirs);

        if (!b)
            continue;
        if (!a || !ours || !theirs || strcmp(ours, theirs)) {
            printf("FAIL export %lu: system has %s, proxy has %s\n", ordinal,
                   theirs ? theirs : "(unnamed)", ours ? ours : "nothing");
            failures++;
        }
        count++;
    }
    CHECK(count > 0);
    printf("%d exports match the system library\n", count);
}

static DWORD WINAPI first_call(void *start)
{
    SYSTEMTIME t = { 2015, 9, 0, 1, 12, 0, 0, 0 };
    wchar_t out[WINHTTP_TIME_FORMAT_BUFSIZE / sizeof(wchar_t) + 1];

    WaitForSingleObject(start, INFINITE);
    return WinHttpTimeFromSystemTime(&t, out) && wcsstr(out, L"2015");
}

/* functions without a wrapper go through the stubs: four register arguments, first from many
 * threads at once */
static void test_stubs(void)
{
    HANDLE start = CreateEventW(NULL, TRUE, FALSE, NULL), threads[16];

    for (int i = 0; i < 16; i++)
        threads[i] = CreateThread(NULL, 0, first_call, start, 0, NULL);
    SetEvent(start);
    WaitForMultipleObjects(16, threads, TRUE, 10000);
    for (int i = 0; i < 16; i++) {
        DWORD result = 0;
        GetExitCodeThread(threads[i], &result);
        CHECK(result == 1);
        CloseHandle(threads[i]);
    }

    for (int pass = 0; pass < 2; pass++) { /* first call resolves, second jumps directly */
        URL_COMPONENTS u = { 0 };
        wchar_t host[64];

        u.dwStructSize = sizeof u;
        u.lpszHostName = host;
        u.dwHostNameLength = 64;
        CHECK(WinHttpCrackUrl(L"https://example.invalid:8443/a", 0, 0, &u));
        CHECK(u.nPort == 8443 && !wcscmp(host, L"example.invalid"));
    }
    SetLastError(0);
    CHECK(!WinHttpCrackUrl(L"nonsense", 0, 0, NULL));
    CHECK(GetLastError() != 0);
}

/* ---------------------------------------------------------------- requests */

static const wchar_t HEADERS[] = L"x-ag-ticket: 0801\r\nAuthorization: SECRETTOKEN\r\n";

static void check_response(HINTERNET request)
{
    DWORD status = 0, size = sizeof status;
    wchar_t value[64];

    CHECK(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              NULL, &status, &size, NULL));
    CHECK(status == 200);
    size = sizeof value;
    CHECK(WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"X-Hydra-Error-Code", value, &size,
                              NULL));
    CHECK(!wcscmp(value, L"200"));
    wchar_t all[512];
    size = sizeof all;
    CHECK(WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL, all, &size, NULL));
    CHECK(wcsstr(all, L"SECRETCOOKIE"));
    size = 0;
    CHECK(!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, NULL, NULL, &size, NULL));
    CHECK(GetLastError() == ERROR_INSUFFICIENT_BUFFER && size > 0);
}

static void test_sync(INTERNET_PORT port)
{
    HINTERNET session = WinHttpOpen(L"harness", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", L"/1/auth/pc_steam?k=SECRETQUERY",
                                           NULL, NULL, NULL, 0);
    char body[64];
    DWORD got = 0, timeout = 5000;

    CHECK(WinHttpCheckPlatform());
    CHECK(session && connection && request);
    CHECK(WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000));
    CHECK(WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof timeout));
    /* four characters, the size of a number: must not appear in the log */
    CHECK(WinHttpSetOption(request, WINHTTP_OPTION_PASSWORD, L"\x5ec1\x5ec2\x5ec3\x5ec4", 4));
    CHECK(WinHttpAddRequestHeaders(request, L"x-test: 1", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD));
    CHECK(WinHttpSendRequest(request, HEADERS, (DWORD)-1, "ping", 4, 4, 0));
    CHECK(WinHttpReceiveResponse(request, NULL));
    check_response(request);
    CHECK(WinHttpQueryDataAvailable(request, &got) && got > 0);
    CHECK(WinHttpReadData(request, body, sizeof body, &got));
    CHECK(got == BODY_SIZE && !memcmp(body, BODY, BODY_SIZE));
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    SetLastError(0);
    CHECK(!WinHttpSendRequest(NULL, NULL, 0, NULL, 0, 0, 0));
    CHECK(GetLastError() == ERROR_INVALID_HANDLE);
}

struct exchange {
    HANDLE done;
    char body[64];
    DWORD got, error;
    int sent, headers, closed;
};

static void CALLBACK on_status(HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID info,
                               DWORD length)
{
    struct exchange *x = (void *)context;

    if (!x)
        return;
    switch (status) {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        x->sent++;
        WinHttpReceiveResponse(handle, NULL);
        break;
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        x->headers++;
        check_response(handle);
        WinHttpQueryDataAvailable(handle, NULL);
        break;
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
        if (*(DWORD *)info)
            WinHttpReadData(handle, x->body + x->got, sizeof x->body - x->got, NULL);
        else
            SetEvent(x->done);
        break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        x->got += length;
        WinHttpQueryDataAvailable(handle, NULL);
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        x->error = ((WINHTTP_ASYNC_RESULT *)info)->dwError;
        SetEvent(x->done);
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        x->closed++;
        SetEvent(x->done);
        break;
    }
}

static void CALLBACK other_status(HINTERNET h, DWORD_PTR c, DWORD s, LPVOID i, DWORD n)
{
    (void)h, (void)c, (void)s, (void)i, (void)n;
}

static void test_async(const wchar_t *host, INTERNET_PORT port, int expect_success)
{
    HINTERNET session = WinHttpOpen(L"harness", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL,
                                    WINHTTP_FLAG_ASYNC);
    struct exchange x = { 0 };

    x.done = CreateEventW(NULL, FALSE, FALSE, NULL);
    /* the caller must get back its own pointers, never the proxy's */
    CHECK(WinHttpSetStatusCallback(session, other_status,
                                   WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) == NULL);
    CHECK(WinHttpSetStatusCallback(session, on_status, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0)
          == other_status);
    HINTERNET connection = WinHttpConnect(session, host, port, 0);
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", L"/1/profile/get", NULL, NULL,
                                           NULL, 0);
    CHECK(session && connection && request);
    BOOL sent = WinHttpSendRequest(request, HEADERS, (DWORD)-1, "ping", 4, 4, (DWORD_PTR)&x);
    CHECK(sent);
    CHECK(WaitForSingleObject(x.done, 30000) == WAIT_OBJECT_0);
    if (expect_success) {
        CHECK(!x.error && x.sent == 1 && x.headers == 1);
        CHECK(x.got == BODY_SIZE && !memcmp(x.body, BODY, BODY_SIZE));
    } else {
        CHECK(x.error != 0 && !x.sent);
    }
    WinHttpCloseHandle(request);
    CHECK(WaitForSingleObject(x.done, 10000) == WAIT_OBJECT_0);
    CHECK(x.closed == 1);
    CHECK(WinHttpSetStatusCallback(session, NULL, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0)
          == on_status);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    CloseHandle(x.done);
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    wchar_t own[1024], loaded[1024], system_path[MAX_PATH + 16];
    HMODULE proxy = GetModuleHandleW(L"winhttp.dll");

    /* the import table must have bound to the copy next to this program */
    GetModuleFileNameW(NULL, own, 1024);
    CHECK(proxy && GetModuleFileNameW(proxy, loaded, 1024));
    *(wcsrchr(own, L'\\') + 1) = L'\0';
    if (failures || _wcsnicmp(own, loaded, wcslen(own))) {
        printf("FAIL: the winhttp.dll in use is not the one next to the test program\n");
        return 1;
    }

    test_stubs();
    GetSystemDirectoryW(system_path, MAX_PATH);
    wcscat(system_path, L"\\winhttp.dll");
    HMODULE real = LoadLibraryW(system_path);
    CHECK(real && real != proxy);
    test_exports(proxy, real);

    INTERNET_PORT port = start_server();
    CHECK(port);
    test_sync(port);
    test_async(L"127.0.0.1", port, 1);
    test_async(L"blackjack.hydra.invalid", 443, 0);

    printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
    return failures != 0;
}
