/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PROXY_H
#define PROXY_H

#include <windows.h>

#include "exports.h"

#define MOD_VERSION "0.2.0"
#define PATH_CHARS 1024

enum {
#define X(index, name) EXPORT_##name = index,
    ALL_EXPORTS(X)
#undef X
};

/* The real function behind an export. Never NULL: if it cannot be found, the result is a
 * function that returns 0 and sets ERROR_PROC_NOT_FOUND. Loads the real library on first use. */
void *real_export(int index);

/* Nonzero once the mod is pinned in memory. Only then may it hand out pointers to its code. */
int stays_loaded(void);

/* "proxy" or "system" if `module` is this DLL or the real winhttp.dll, else NULL. Both have the
 * same file name. */
const char *winhttp_role(HMODULE module);

/* The full path of one of the mod's files, which live in the same folder as the log.
 * `out` holds PATH_CHARS characters. Returns 0 if there is no usable folder. */
int mod_file(wchar_t *out, const wchar_t *name);

/* One line in the log. Keeps GetLastError() unchanged. */
void log_line(const char *format, ...) __attribute__((format(gnu_printf, 1, 2)));

#endif
