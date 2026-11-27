/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Which hosts go to the listener, and which connection handles lead there. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <wchar.h>

#include "redirect.h"

#define CONNECTIONS 64

static SRWLOCK lock = SRWLOCK_INIT;
static HINTERNET held[CONNECTIONS];

int redirect_host(const wchar_t *server)
{
    return server
           && (!_wcsicmp(server, L"blackjack.hydra.agoragames.com")
               || !_wcsicmp(server, L"cls.psn.turbine.com"));
}

/* replace the first `from` with `to`; returns whether there was one */
static int swap(HINTERNET from, HINTERNET to)
{
    int found = 0;

    AcquireSRWLockExclusive(&lock);
    for (int i = 0; i < CONNECTIONS && !found; i++)
        if (held[i] == from) {
            held[i] = to;
            found = 1;
        }
    ReleaseSRWLockExclusive(&lock);
    return found;
}

int redirect_remember(HINTERNET connection)
{
    redirect_forget(connection);
    return swap(NULL, connection);
}

void redirect_forget(HINTERNET handle)
{
    if (handle)
        swap(handle, NULL);
}

int redirected(HINTERNET connection)
{
    int found = 0;

    AcquireSRWLockShared(&lock);
    for (int i = 0; i < CONNECTIONS && !found; i++)
        found = connection && held[i] == connection;
    ReleaseSRWLockShared(&lock);
    return found;
}
