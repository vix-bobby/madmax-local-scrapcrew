/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * winhttp.dll proxy for Mad Max: loads the system winhttp.dll and passes every call to it.
 * Adds files only; changes no game file.
 *
 * The calls for the game's two online hosts are sent to a listener inside this DLL instead
 * (redirect.c, server.c).
 *
 * Nothing happens in DllMain. The first call to any export initialises the mod.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#include "proxy.h"

#define LOG_NAME L"madmax_scrapcrew.log"
#define LOG_LIMIT (4 * 1024 * 1024)

static HMODULE self;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION log_lock;
static wchar_t log_path[PATH_CHARS], mod_dir[PATH_CHARS];
static long log_size;
static int pinned_ok;

/* external linkage: the stubs below jump through this table */
void *export_slot[EXPORT_COUNT];

static const char *const export_name[EXPORT_COUNT] = {
#define X(index, name) #name,
    ALL_EXPORTS(X)
#undef X
};

/* ---------------------------------------------------------------- log */

static void write_log(const char *format, va_list args)
{
    FILE *f;
    SYSTEMTIME t;
    int n;

    if (!log_path[0])
        return;
    EnterCriticalSection(&log_lock);
    if (log_size < LOG_LIMIT && (f = _wfopen(log_path, L"a"))) {
        GetLocalTime(&t);
        n = fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu]  ", t.wYear, t.wMonth, t.wDay,
                    t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());
        n += vfprintf(f, format, args);
        fputc('\n', f);
        log_size += n + 1;
        if (log_size >= LOG_LIMIT)
            fputs("log size limit reached: nothing more is written\n", f);
        fclose(f);
    }
    LeaveCriticalSection(&log_lock);
}

static void log_raw(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    write_log(format, args);
    va_end(args);
}

/* The log goes next to the DLL, or into %TEMP% if that folder is read-only. */
static int start_log(const wchar_t *dir)
{
    FILE *f;

    if (_snwprintf(log_path, PATH_CHARS, L"%ls%ls", dir, LOG_NAME) < 0)
        return 0;
    log_path[PATH_CHARS - 1] = L'\0';
    if (!(f = _wfopen(log_path, L"w")))
        return 0;
    fclose(f);
    wcscpy(mod_dir, dir);
    return 1;
}

/* ---------------------------------------------------------------- initialisation */

static HMODULE real_library;

static BOOL CALLBACK initialise(PINIT_ONCE o, PVOID p, PVOID *c)
{
    wchar_t dir[PATH_CHARS], temp[MAX_PATH + 1], path[MAX_PATH + 16];
    wchar_t *slash;
    HMODULE pinned;
    DWORD n;

    (void)o, (void)p, (void)c;
    InitializeCriticalSection(&log_lock);

    n = GetModuleFileNameW(self, dir, PATH_CHARS);
    dir[n < PATH_CHARS ? n : PATH_CHARS - 1] = L'\0';
    slash = wcsrchr(dir, L'\\');
    if (slash)
        slash[1] = L'\0';
    if (!start_log(dir)) {
        n = GetTempPathW(MAX_PATH + 1, temp);
        if (!n || n > MAX_PATH || !start_log(temp))
            log_path[0] = L'\0';
    }
    log_raw("madmax scrap crew mod " MOD_VERSION "%s loaded", TRACING ? " (trace build)" : "");

    /* never unload: WinHTTP keeps pointers to our status callbacks */
    pinned_ok = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN
                                       | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                   (LPCWSTR)(void *)initialise, &pinned)
                != 0;
    if (!pinned_ok)
        log_raw("cannot keep the mod loaded (error %lu): callbacks are not traced",
                GetLastError());

    n = GetSystemDirectoryW(path, MAX_PATH);
    if (n && n < MAX_PATH) {
        wcscat(path, L"\\winhttp.dll");
        real_library = LoadLibraryW(path);
    }
    if (real_library == self) /* we are installed in the system folder: nothing to forward to */
        real_library = NULL;
    if (!real_library)
        log_raw("system winhttp.dll: NOT LOADED");
    return TRUE;
}

static void ensure_initialised(void)
{
    DWORD error = GetLastError();

    InitOnceExecuteOnce(&once, initialise, NULL, NULL);
    SetLastError(error);
}

int stays_loaded(void)
{
    ensure_initialised();
    return pinned_ok;
}

int mod_file(wchar_t *out, const wchar_t *name)
{
    ensure_initialised();
    if (!mod_dir[0] || _snwprintf(out, PATH_CHARS, L"%ls%ls", mod_dir, name) < 0)
        return 0;
    out[PATH_CHARS - 1] = L'\0';
    return 1;
}

const char *winhttp_role(HMODULE module)
{
    ensure_initialised();
    if (module == self)
        return "proxy";
    return module && module == real_library ? "system" : NULL;
}

void log_line(const char *format, ...)
{
    DWORD error = GetLastError();
    va_list args;

    ensure_initialised();
    va_start(args, format);
    write_log(format, args);
    va_end(args);
    SetLastError(error);
}

/* ---------------------------------------------------------------- forwarding */

/* stands in for an export the real library does not have */
static INT_PTR WINAPI unavailable(void)
{
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

void *real_export(int index)
{
    void *found = __atomic_load_n(&export_slot[index], __ATOMIC_ACQUIRE);
    DWORD error;

    if (found)
        return found;
    ensure_initialised();
    error = GetLastError();
    if (real_library)
        found = (void *)GetProcAddress(real_library, export_name[index]);
    if (!found) {
        log_raw("not available in the system winhttp.dll: %s", export_name[index]);
        found = (void *)unavailable;
    }
    __atomic_store_n(&export_slot[index], found, __ATOMIC_RELEASE);
    SetLastError(error);
    return found;
}

/*
 * One stub per export without a wrapper. It knows neither the arguments nor their number, so it
 * jumps to the real function with registers and stack as the caller left them. On the first call
 * the four argument registers (integer and floating point) are saved around real_export().
 */
__asm__(".text\n"
        ".p2align 4\n"
        "resolve_stub:\n" /* in: eax = export index */
        ".seh_proc resolve_stub\n"
        "    subq $0x88, %rsp\n"
        ".seh_stackalloc 0x88\n"
        ".seh_endprologue\n"
        "    movq %rcx, 0x20(%rsp)\n"
        "    movq %rdx, 0x28(%rsp)\n"
        "    movq %r8,  0x30(%rsp)\n"
        "    movq %r9,  0x38(%rsp)\n"
        "    movdqu %xmm0, 0x40(%rsp)\n"
        "    movdqu %xmm1, 0x50(%rsp)\n"
        "    movdqu %xmm2, 0x60(%rsp)\n"
        "    movdqu %xmm3, 0x70(%rsp)\n"
        "    movl %eax, %ecx\n"
        "    call real_export\n"
        "    movq 0x20(%rsp), %rcx\n"
        "    movq 0x28(%rsp), %rdx\n"
        "    movq 0x30(%rsp), %r8\n"
        "    movq 0x38(%rsp), %r9\n"
        "    movdqu 0x40(%rsp), %xmm0\n"
        "    movdqu 0x50(%rsp), %xmm1\n"
        "    movdqu 0x60(%rsp), %xmm2\n"
        "    movdqu 0x70(%rsp), %xmm3\n"
        "    addq $0x88, %rsp\n"
        "    jmpq *%rax\n"
        ".seh_endproc\n");

#define STRINGIFY(x) #x
#define STUB(index, name)                                                                        \
    __asm__(".text\n"                                                                            \
            ".p2align 4\n"                                                                       \
            ".globl fw_" #name "\n"                                                              \
            "fw_" #name ":\n"                                                                    \
            "    movq export_slot+8*" STRINGIFY(index) "(%rip), %rax\n"                          \
            "    testq %rax, %rax\n"                                                             \
            "    jz 1f\n"                                                                        \
            "    jmpq *%rax\n"                                                                   \
            "1:  movl $" STRINGIFY(index) ", %eax\n"                                             \
            "    jmp resolve_stub\n");
STUB_EXPORTS(STUB)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        self = instance;
    return TRUE;
}
