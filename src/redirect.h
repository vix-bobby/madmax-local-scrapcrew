/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REDIRECT_H
#define REDIRECT_H

#include <windows.h>
#include <winhttp.h>

/* nonzero for one of the two hosts this mod answers itself */
int redirect_host(const wchar_t *server);

/* Remember, or forget, that a connection handle leads to the listener. Call for every new
 * connection, whatever its host, and before closing any handle: WinHTTP reuses the values.
 * redirect_remember returns 0 if there is no room left. */
int redirect_remember(HINTERNET connection);
void redirect_forget(HINTERNET handle);
int redirected(HINTERNET connection);

#endif
