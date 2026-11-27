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

/* ---------------------------------------------------------------- the game's two hosts */

/* a proxy that does not exist: nothing sent to the two hosts can leave the machine, and only a
 * request that was redirected and told to use no proxy can succeed */
#define DEAD_PROXY L"127.0.0.1:9"
#define HYDRA L"blackjack.hydra.agoragames.com"
#define SOAP L"cls.psn.turbine.com"
/* made-up account 76561190000000001 */
#define ACCOUNT_TEXT "\x0e\x00\x00\x00\x11" "76561190000000001"
#define ACCOUNT_ID "\x0d\x01\x0f\xff\xff\x25\x87\xfc\x01"
#define SCRAP "\x0e\x00\x00\x00\x18" "timestamp_scrap_rewarded"
#define BYTES(s) s, (DWORD)sizeof s - 1

struct fetch {
    HANDLE done, closed;
    char body[8192];
    DWORD status, got, error;
    wchar_t type[64], accept[64], time[16], hydra_code[16];
};

static void CALLBACK on_fetch(HINTERNET handle, DWORD_PTR context, DWORD status, LPVOID info,
                              DWORD length)
{
    struct fetch *f = (void *)context;
    DWORD size = sizeof f->status;

    if (!f)
        return;
    switch (status) {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        WinHttpReceiveResponse(handle, NULL);
        break;
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL,
                            &f->status, &size, NULL);
        size = sizeof f->type;
        WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_TYPE, NULL, f->type, &size, NULL);
        size = sizeof f->accept;
        WinHttpQueryHeaders(handle, WINHTTP_QUERY_CUSTOM, L"Accept", f->accept, &size, NULL);
        size = sizeof f->time;
        WinHttpQueryHeaders(handle, WINHTTP_QUERY_CUSTOM, L"X-Hydra-Processing-Time", f->time,
                            &size, NULL);
        size = sizeof f->hydra_code;
        WinHttpQueryHeaders(handle, WINHTTP_QUERY_CUSTOM, L"X-Hydra-Error-Code", f->hydra_code,
                            &size, NULL);
        WinHttpReadData(handle, f->body, 4096, NULL);
        break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        f->got += length;
        if (length && f->got + 4096 <= sizeof f->body)
            WinHttpReadData(handle, f->body + f->got, 4096, NULL);
        else
            SetEvent(f->done);
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        f->error = ((WINHTTP_ASYNC_RESULT *)info)->dwError;
        SetEvent(f->done);
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        SetEvent(f->closed);
        break;
    }
}

static int url_starts(HINTERNET request, const wchar_t *start)
{
    wchar_t url[512];
    DWORD size = sizeof url;

    return WinHttpQueryOption(request, WINHTTP_OPTION_URL, url, &size)
           && !wcsncmp(url, start, wcslen(start));
}

/* one call, made the way the game makes it */
static void fetch(HINTERNET session, const wchar_t *host, const wchar_t *verb,
                  const wchar_t *path, const wchar_t *headers, const char *body, DWORD length,
                  struct fetch *f)
{
    HINTERNET connection = WinHttpConnect(session, host, 443, 0);
    HINTERNET request = WinHttpOpenRequest(connection, verb, path, NULL, NULL, NULL,
                                           WINHTTP_FLAG_SECURE);
    DWORD ignore = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                   | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;

    memset(f, 0, sizeof *f);
    f->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    f->closed = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(connection && request);
    if (!wcscmp(host, SOAP))
        CHECK(WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &ignore, sizeof ignore));
    WinHttpSetStatusCallback(request, on_fetch, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);
    if (WinHttpSendRequest(request, headers, headers ? (DWORD)-1 : 0, (void *)body, length,
                           length, (DWORD_PTR)f))
        CHECK(WaitForSingleObject(f->done, 30000) == WAIT_OBJECT_0);
    else
        f->error = GetLastError();
    WinHttpCloseHandle(request);
    CHECK(WaitForSingleObject(f->closed, 10000) == WAIT_OBJECT_0);
    WinHttpCloseHandle(connection);
    CloseHandle(f->done);
    CloseHandle(f->closed);
}

static struct fetch answer;

static int hydra(HINTERNET session, const wchar_t *route, const char *body, DWORD length,
                 const char *expected, DWORD expected_length)
{
    wchar_t path[256];

    _snwprintf(path, 256, L"/984815842354416438/%ls", route);
    fetch(session, HYDRA, body ? L"POST" : L"GET", path, L"x-ag-ticket: 0801", body, length,
          &answer);
    return !answer.error && answer.status == 200 && !wcscmp(answer.hydra_code, L"200")
           && !wcscmp(answer.type, L"application/x-hydra-binary")
           && !wcscmp(answer.accept, answer.type) && answer.time[0]
           && answer.got == expected_length
           && !memcmp(answer.body, expected, expected_length);
}

#define HYDRA_CALL(route, body, expected)                                                        \
    CHECK(hydra(session, route, BYTES(body), BYTES(expected)))

static DWORD WINAPI parallel_call(void *session)
{
    struct fetch f;

    fetch(session, HYDRA, L"POST", L"/1/event_log/create_events", NULL, "x", 1, &f);
    return !f.error && f.status == 200 && f.got == 1;
}

/* the reward value of profile/get: "0" if never rewarded, else close to the largest number */
static long long reward_value(HINTERNET session)
{
    static const char query[] = ACCOUNT_ID "\x08\x00\x00\x00\x01" SCRAP;
    const DWORD value_at = 5 + 29 + 5; /* map, name, text length */

    fetch(session, HYDRA, L"POST", L"/1/profile/get", NULL, BYTES(query), &answer);
    if (answer.error || answer.status != 200 || answer.got <= value_at)
        return -1;
    answer.body[answer.got] = '\0';
    return _atoi64(answer.body + value_at);
}

static void test_redirect(int again)
{
    HINTERNET session = WinHttpOpen(L"harness", WINHTTP_ACCESS_TYPE_NAMED_PROXY, DEAD_PROXY, NULL,
                                    WINHTTP_FLAG_ASYNC);

    CHECK(session);
    /* if this fails, stop: the other calls have nowhere to go */
    if (!hydra(session, L"auth/pc_steam", BYTES("\x0e\x00\x00\x00\x04" "0802"),
               BYTES("\x0e\x00\x00\x00\x04" "0801"))) {
        printf("FAIL: the call to the Hydra host was not answered here (error %lu, status %lu)\n",
               answer.error, answer.status);
        failures++;
        return;
    }

    if (again) { /* a second launch: the profile and the reward time were kept */
        long long value = reward_value(session);
        CHECK(value > 2147483647LL - 600 && value <= 2147483647LL);
        WinHttpCloseHandle(session);
        return;
    }

    HYDRA_CALL(L"profile/get_by_platform_account_id",
               ACCOUNT_TEXT "\x08\x00\x00\x00\x01\x0e\x00\x00\x00\x04" "guid",
               "\x0c\x00\x00\x00\x00");
    HYDRA_CALL(L"profile/create",
               "\x0c\x00\x00\x00\x02"
               "\x0e\x00\x00\x00\x0c" "display_name" "\x0e\x00\x00\x00\x04" "Test"
               "\x0e\x00\x00\x00\x13" "platform_account_id" ACCOUNT_TEXT,
               ACCOUNT_ID);
    CHECK(hydra(session, L"feed/get_items_by_channel/TOS%5FZZ/1/1/0", NULL, 0,
                BYTES("\x08\x00\x00\x00\x00")));
    CHECK(reward_value(session) == 0);
    HYDRA_CALL(L"profile/update",
               ACCOUNT_ID "\x0c\x00\x00\x00\x01" SCRAP "\x0e\x00\x00\x00\x01" "1", "\x01");
    long long value = reward_value(session);
    CHECK(value > 2147483647LL - 60 && value <= 2147483647LL);

    static const char lookup[] = "<soap:Body><ams:LookupWbid><ams:consoleId>76561190000000001"
                                 "</ams:consoleId></ams:LookupWbid></soap:Body>";
    fetch(session, SOAP, L"POST", L"/CLS/WbAccountManagement.asmx",
          L"Content-Type: text/xml;charset=UTF-8\r\n"
          L"SOAPAction: http://www.turbine.com/SE/AMS/LookupWbid",
          BYTES(lookup), &answer);
    CHECK(!answer.error && answer.status == 200);
    CHECK(!wcscmp(answer.type, L"text/xml; charset=utf-8"));
    answer.body[answer.got] = '\0';
    CHECK(strstr(answer.body, "STEAM_76561190000000001@example.com"));

    fetch(session, HYDRA, L"POST", L"/1/nothing/here", NULL, "x", 1, &answer);
    CHECK(!answer.error && answer.status == 404 && !answer.got);

    /* a body larger than one packet */
    static char large[100000];
    fetch(session, HYDRA, L"POST", L"/1/event_log/create_events", NULL, large, sizeof large,
          &answer);
    CHECK(!answer.error && answer.status == 200 && answer.got == 1);

    HANDLE threads[16];
    for (int i = 0; i < 16; i++)
        threads[i] = CreateThread(NULL, 0, parallel_call, session, 0, NULL);
    WaitForMultipleObjects(16, threads, TRUE, 60000);
    for (int i = 0; i < 16; i++) {
        DWORD result = 0;
        GetExitCodeThread(threads[i], &result);
        CHECK(result == 1);
        CloseHandle(threads[i]);
    }

    /* every other host is left alone: this one still goes to the dead proxy */
    fetch(session, L"other.invalid", L"GET", L"/", NULL, NULL, 0, &answer);
    CHECK(answer.error);
    WinHttpCloseHandle(session);
}

/* a handle value that led to the listener, reused for something else, must not lead there */
static void test_handle_reuse(INTERNET_PORT port)
{
    HINTERNET session = WinHttpOpen(L"harness", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    HINTERNET seen[100];
    int reused = 0;

    for (int i = 0; i < 100; i++) {
        HINTERNET ours = WinHttpConnect(session, HYDRA, 443, 0);
        HINTERNET request = WinHttpOpenRequest(ours, L"GET", L"/", NULL, NULL, NULL,
                                               WINHTTP_FLAG_SECURE);
        CHECK(url_starts(request, L"http://127.0.0.1"));
        seen[i] = ours;
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(ours);

        HINTERNET other = WinHttpConnect(session, L"127.0.0.1", port, 0);
        request = WinHttpOpenRequest(other, L"GET", L"/", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
        CHECK(url_starts(request, L"https://127.0.0.1"));
        for (int k = 0; k <= i; k++)
            reused += seen[k] == other;
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(other);
    }
    printf("handle values reused by an unrelated connection: %d times\n", reused);
    WinHttpCloseHandle(session);
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    int again = argc > 1 && !strcmp(argv[1], "again");
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

    if (again) {
        test_redirect(1);
        printf(failures ? "%d FAILED\n" : "second launch tests passed\n", failures);
        return failures != 0;
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
    test_redirect(0);
    test_handle_reuse(port);

    printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
    return failures != 0;
}
