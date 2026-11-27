/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SERVER_H
#define SERVER_H

/* The port of the listener on 127.0.0.1, starting it on first use. 0 if it cannot run. */
unsigned short server_port(void);

#endif
