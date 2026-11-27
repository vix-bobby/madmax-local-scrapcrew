/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The answers to the game's online calls, and the little that has to be remembered between
 * them. Standard C only: no sockets, files or clock.
 */
#ifndef RESPONDER_H
#define RESPONDER_H

#include <stddef.h>
#include <time.h>

#define REPLY_SIZE 4096
#define STATE_TEXT_SIZE 4096
#define ACCOUNT_SIZE 24

/* one profile field, kept as the encoded value the game sent */
struct value {
    unsigned char bytes[768];
    size_t length; /* 0: none */
};

struct state {
    char account[ACCOUNT_SIZE]; /* SteamID64 in decimal; empty: no profile yet */
    struct value display_name, dlcs, age_group;
    unsigned long long updated_at;     /* UTC as YYYYMMDDHHmmss; 0: never */
    unsigned long long scrap_rewarded; /* same; 0: never rewarded */
};

struct request {
    const char *method, *path;
    const char *soap_action; /* "" if there is none */
    const unsigned char *body;
    size_t length;
};

struct reply {
    int status;
    const char *content_type;
    unsigned char body[REPLY_SIZE];
    size_t length;
};

/* Fill `reply` for `request`. Returns nonzero if `state` changed. `now` is the current time. */
int respond(struct state *state, const struct request *request, time_t now, struct reply *reply);

/* The state as text, and back. state_write returns the length, or 0 if `size` is too small.
 * state_read ignores what it does not understand. */
size_t state_write(const struct state *state, char *out, size_t size);
void state_read(struct state *state, const char *text);

#endif
