/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Parts of this file follow madmax-2015-offline (https://github.com/3ek/madmax-2015-offline,
 * commit fee927e), Copyright (C) 2025 madmax-2015-offline contributors, GPL-3.0-or-later,
 * rewritten in C: the shape of the replies and the reward timestamp rule. See docs/credits.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "responder.h"

enum {
    T_NONE = 0x01, T_INT32 = 0x02, T_BINARY = 0x03, T_BOOL = 0x04, T_BYTE = 0x05,
    T_FLOAT = 0x06, T_TIME = 0x07, T_ARRAY = 0x08, T_STRUCT = 0x09, T_INT64 = 0x0a,
    T_BITS = 0x0b, T_MAP = 0x0c, T_UINT64 = 0x0d, T_TEXT = 0x0e,
};

#define LONGEST_WAIT 2147483647LL /* the game computes: elapsed = this - our value */
#define MAX_DEPTH 8
#define MAX_FIELDS 64

/* ---------------------------------------------------------------- reading Hydra values */

struct cursor {
    const unsigned char *at, *end;
};

static unsigned long long number(const unsigned char *at, int bytes)
{
    unsigned long long n = 0;

    while (bytes--)
        n = n << 8 | *at++;
    return n;
}

static int take(struct cursor *c, size_t length, const unsigned char **out)
{
    if ((size_t)(c->end - c->at) < length)
        return 0;
    if (out)
        *out = c->at;
    c->at += length;
    return 1;
}

/* the type byte and, for containers and texts, the count or length that follows it */
static int head(struct cursor *c, int type, unsigned long *count)
{
    const unsigned char *p;

    if (!take(c, 5, &p) || p[0] != type)
        return 0;
    *count = (unsigned long)number(p + 1, 4);
    return 1;
}

static int skip(struct cursor *c, int depth)
{
    const unsigned char *p;
    unsigned long count;
    int type;

    if (depth > MAX_DEPTH || c->at >= c->end)
        return 0;
    type = *c->at;
    switch (type) {
    case T_NONE: return take(c, 1, NULL);
    case T_BOOL: case T_BYTE: return take(c, 2, NULL);
    case T_INT32: case T_TIME: return take(c, 5, NULL);
    case T_FLOAT: case T_INT64: case T_UINT64: return take(c, 9, NULL);
    case T_TEXT: case T_BINARY: case T_BITS:
        return head(c, type, &count) && take(c, count, NULL);
    case T_ARRAY: case T_STRUCT: case T_MAP:
        if (!take(c, 1, NULL) || !take(c, 4, &p))
            return 0;
        count = (unsigned long)number(p, 4);
        if (count > (size_t)(c->end - c->at))
            return 0;
        if (type != T_ARRAY)
            count *= 2;
        while (count--)
            if (!skip(c, depth + 1))
                return 0;
        return 1;
    }
    return 0;
}

struct text {
    const char *at;
    size_t length;
};

static int read_text(struct cursor *c, struct text *out)
{
    unsigned long length;
    const unsigned char *p;

    if (!head(c, T_TEXT, &length) || !take(c, length, &p))
        return 0;
    out->at = (const char *)p;
    out->length = length;
    return 1;
}

static int is(struct text t, const char *word)
{
    return t.length == strlen(word) && !memcmp(t.at, word, t.length);
}

static int read_uint64(struct cursor *c, unsigned long long *out)
{
    const unsigned char *p;

    if (!take(c, 9, &p) || p[0] != T_UINT64)
        return 0;
    *out = number(p + 1, 8);
    return 1;
}

static int read_names(struct cursor *c, struct text *names, size_t *count)
{
    unsigned long n;

    if (!head(c, T_ARRAY, &n) || n > MAX_FIELDS)
        return 0;
    for (*count = 0; *count < n; ++*count)
        if (!read_text(c, &names[*count]))
            return 0;
    return 1;
}

/* a SteamID64 as text: digits only, short enough to store */
static int account_of(struct text t, char *out)
{
    if (!t.length || t.length >= ACCOUNT_SIZE)
        return 0;
    for (size_t i = 0; i < t.length; i++)
        if (t.at[i] < '0' || t.at[i] > '9')
            return 0;
    memcpy(out, t.at, t.length);
    out[t.length] = '\0';
    return 1;
}

/* ---------------------------------------------------------------- writing */

/* a reply too long for its buffer becomes an error */
static void put(struct reply *r, const void *data, size_t length)
{
    if (r->status != 200 || length > sizeof r->body - r->length) {
        r->status = 500;
        r->length = 0;
        return;
    }
    memcpy(r->body + r->length, data, length);
    r->length += length;
}

static void put_byte(struct reply *r, int byte)
{
    unsigned char b = (unsigned char)byte;

    put(r, &b, 1);
}

static void put_number(struct reply *r, unsigned long long n, int bytes)
{
    while (bytes--)
        put_byte(r, (int)(n >> (8 * bytes) & 0xff));
}

static void put_head(struct reply *r, int type, size_t count)
{
    put_byte(r, type);
    put_number(r, count, 4);
}

static void put_text(struct reply *r, const char *at, size_t length)
{
    put_head(r, T_TEXT, length);
    put(r, at, length);
}

static void put_uint64(struct reply *r, unsigned long long n)
{
    put_byte(r, T_UINT64);
    put_number(r, n, 8);
}

static void put_string(struct reply *r, const char *s)
{
    put(r, s, strlen(s));
}

/* ---------------------------------------------------------------- time */

/* days since 1970-01-01 of a date in the proleptic Gregorian calendar */
static long long days_from_date(long long year, int month, int day)
{
    year -= month <= 2;
    long long era = (year >= 0 ? year : year - 399) / 400;
    long long of_era = year - era * 400;
    long long of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;

    return era * 146097 + of_era * 365 + of_era / 4 - of_era / 100 + of_year - 719468;
}

static unsigned long long game_time(time_t now)
{
    struct tm *t = gmtime(&now);

    if (!t)
        return 0;
    return (t->tm_year + 1900) * 10000000000ULL + (t->tm_mon + 1) * 100000000ULL
           + t->tm_mday * 1000000ULL + t->tm_hour * 10000ULL + t->tm_min * 100ULL + t->tm_sec;
}

/* Seconds since 1970 of a YYYYMMDDHHmmss time; -1 if it is not one. */
static long long seconds_of(unsigned long long stamp)
{
    int second = (int)(stamp % 100), minute = (int)(stamp / 100 % 100);
    int hour = (int)(stamp / 10000 % 100), day = (int)(stamp / 1000000 % 100);
    int month = (int)(stamp / 100000000 % 100);
    unsigned long long year = stamp / 10000000000ULL;

    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31
        || hour > 23 || minute > 59 || second > 60)
        return -1;
    return days_from_date((long long)year, month, day) * 86400 + hour * 3600 + minute * 60
           + second;
}

/* The value of timestamp_scrap_rewarded: the longest wait minus the seconds since the last
 * reward, or "0" if there never was one. A clock set back counts as no time passed. */
static void put_reward_value(struct reply *r, const struct state *state, time_t now)
{
    long long last = state->scrap_rewarded ? seconds_of(state->scrap_rewarded) : -1;
    long long value = 0;
    char text[24];

    if (last >= 0) {
        long long elapsed = (long long)now - last;
        if (elapsed < 0)
            elapsed = 0;
        value = elapsed < LONGEST_WAIT ? LONGEST_WAIT - elapsed : 0;
    }
    put_text(r, text, (size_t)snprintf(text, sizeof text, "%lld", value));
}

/* ---------------------------------------------------------------- profile */

/* start over if the call is about another account */
static int use_account(struct state *state, const char *account)
{
    if (!strcmp(state->account, account))
        return 0;
    memset(state, 0, sizeof *state);
    strcpy(state->account, account);
    return 1;
}

static struct value *stored_field(struct state *state, struct text name)
{
    if (is(name, "display_name"))
        return &state->display_name;
    if (is(name, "dlcs"))
        return &state->dlcs;
    if (is(name, "age_group"))
        return &state->age_group;
    return NULL;
}

/* Apply a map of changes. Only the fields this mod hands back are kept. */
static int apply(struct state *state, struct cursor *c, time_t now)
{
    unsigned long pairs;

    if (!head(c, T_MAP, &pairs) || pairs > MAX_FIELDS)
        return 0;
    while (pairs--) {
        struct text name;
        if (!read_text(c, &name))
            return 0;
        const unsigned char *start = c->at;
        if (!skip(c, 0))
            return 0;
        size_t length = (size_t)(c->at - start);
        struct value *field = stored_field(state, name);
        if (is(name, "timestamp_scrap_rewarded")) {
            /* our clock decides, not the value sent */
            state->scrap_rewarded = game_time(now);
        } else if (field && length <= sizeof field->bytes) {
            memcpy(field->bytes, start, length);
            field->length = length;
        }
    }
    state->updated_at = game_time(now);
    return 1;
}

static int create_profile(struct state *state, struct cursor c, time_t now, struct reply *r)
{
    struct cursor look = c;
    char account[ACCOUNT_SIZE] = "";
    unsigned long pairs;

    if (!head(&look, T_MAP, &pairs) || pairs > MAX_FIELDS)
        return -1;
    while (pairs--) {
        struct text name, value;
        if (!read_text(&look, &name))
            return -1;
        if (is(name, "platform_account_id")) {
            if (!read_text(&look, &value) || !account_of(value, account))
                return -1;
        } else if (!skip(&look, 0)) {
            return -1;
        }
    }
    if (!account[0])
        return -1;
    use_account(state, account);
    if (!apply(state, &c, now))
        return -1;
    put_uint64(r, strtoull(account, NULL, 10));
    return 1;
}

static int get_by_platform_id(struct state *state, struct cursor c, struct reply *r)
{
    struct text id, names[MAX_FIELDS];
    size_t count, found = 0;
    char account[ACCOUNT_SIZE];

    if (!read_text(&c, &id) || !account_of(id, account) || !read_names(&c, names, &count))
        return -1;
    if (strcmp(state->account, account) || !state->display_name.length) {
        put_head(r, T_MAP, 0); /* no profile: the game creates one */
        return 0;
    }
    /* count the fields that can be answered, then write them */
    for (int write = 0; write < 2; write++) {
        if (write)
            put_head(r, T_MAP, found);
        for (size_t i = 0; i < count; i++) {
            struct text n = names[i];
            const struct value *v = NULL;
            int guid = is(n, "guid");
            int wb = is(n, "wb_id") || is(n, "wb_account_id") || is(n, "wb_subscription_id");

            if (is(n, "name"))
                v = &state->display_name;
            else if (is(n, "dlcs"))
                v = &state->dlcs;
            else if (is(n, "age_group"))
                v = &state->age_group;
            if (!guid && !wb && !(v && v->length))
                continue;
            if (!write) {
                found++;
                continue;
            }
            put_text(r, n.at, n.length);
            if (guid) {
                put_uint64(r, strtoull(account, NULL, 10));
            } else if (wb) {
                put_text(r, account, strlen(account));
            } else if (v == &state->age_group && v->length == 5 && v->bytes[0] == T_INT32) {
                /* sent as 32 bits; the reply the game is known to accept has 64 */
                unsigned long long n32 = number(v->bytes + 1, 4);
                put_byte(r, T_INT64);
                put_number(r, n32 & 0x80000000UL ? n32 | 0xffffffff00000000ULL : n32, 8);
            } else {
                put(r, v->bytes, v->length);
            }
        }
    }
    return 0;
}

/* profile/get and profile/update both start with the account id */
static int read_account(struct cursor *c, char *account)
{
    unsigned long long id;

    if (!read_uint64(c, &id) || !id)
        return 0;
    snprintf(account, ACCOUNT_SIZE, "%llu", id);
    return 1;
}

static int get_profile(struct state *state, struct cursor c, time_t now, struct reply *r)
{
    struct text names[MAX_FIELDS];
    size_t count;
    char account[ACCOUNT_SIZE];

    if (!read_account(&c, account) || !read_names(&c, names, &count))
        return -1;
    int changed = use_account(state, account);
    put_head(r, T_MAP, count);
    for (size_t i = 0; i < count; i++) {
        struct text n = names[i];
        put_text(r, n.at, n.length);
        if (is(n, "timestamp_scrap_rewarded")) {
            put_reward_value(r, state, now);
        } else if (is(n, "updated_at") || is(n, "timestamp_trigger")) {
            if (!state->updated_at) {
                state->updated_at = game_time(now);
                changed = 1;
            }
            put_uint64(r, state->updated_at);
        } else if (is(n, "guid") || is(n, "id")) {
            put_uint64(r, strtoull(account, NULL, 10));
        } else {
            put_byte(r, T_NONE);
        }
    }
    return changed;
}

static int update_profile(struct state *state, struct cursor c, time_t now, struct reply *r)
{
    char account[ACCOUNT_SIZE];

    if (!read_account(&c, account))
        return -1;
    use_account(state, account);
    if (!apply(state, &c, now))
        return -1;
    put_byte(r, T_NONE);
    return 1;
}

/* ---------------------------------------------------------------- feeds */

/* Every requested field of a news feed is answered with an empty text. */
static int feed(struct cursor c, struct reply *r)
{
    struct text channel, names[MAX_FIELDS];
    size_t count;

    if (!read_text(&c, &channel))
        return -1;
    /* some numbers precede the list of field names */
    while (c.at < c.end && *c.at != T_ARRAY)
        if (!skip(&c, 0))
            return -1;
    if (!read_names(&c, names, &count))
        return -1;
    put_head(r, T_MAP, count);
    for (size_t i = 0; i < count; i++) {
        put_text(r, names[i].at, names[i].length);
        put_text(r, "", 0);
    }
    return 0;
}

/* Returns 1 if the state changed, 0 if not, -1 for a request that cannot be understood. */
static int hydra(struct state *state, const char *route, const struct request *q, time_t now,
                 struct reply *r)
{
    struct cursor body = { q->body, q->body + q->length };

    if (!strcmp(q->method, "GET") && !strncmp(route, "feed/get_items_by_channel/", 26)) {
        put_head(r, T_ARRAY, 0); /* terms of service and privacy policy: nothing to accept */
        return 0;
    }
    if (strcmp(q->method, "POST"))
        r->status = 404;
    else if (!strcmp(route, "auth/pc_steam"))
        put_text(r, "0801", 4); /* a session ticket; the game only sends it back */
    else if (!strcmp(route, "profile/get_by_platform_account_id"))
        return get_by_platform_id(state, body, r);
    else if (!strcmp(route, "profile/create"))
        return create_profile(state, body, now, r);
    else if (!strcmp(route, "profile/get"))
        return get_profile(state, body, now, r);
    else if (!strcmp(route, "profile/update"))
        return update_profile(state, body, now, r);
    else if (!strcmp(route, "feed/get_items_by_channel"))
        return feed(body, r);
    else if (!strcmp(route, "event_log/create_events"))
        put_byte(r, T_NONE);
    else
        r->status = 404;
    return 0;
}

/* ---------------------------------------------------------------- SOAP */

/* the digits inside <ams:tag>...</ams:tag> */
static int digits_in(const struct request *q, const char *tag, char *out)
{
    char open[48];
    size_t n = (size_t)snprintf(open, sizeof open, "<ams:%s>", tag);

    for (size_t i = 0; i + n <= q->length; i++) {
        if (memcmp(q->body + i, open, n))
            continue;
        struct text t = { (const char *)q->body + i + n, 0 };
        while (i + n + t.length < q->length && t.at[t.length] != '<')
            t.length++;
        return account_of(t, out);
    }
    return 0;
}

static void soap(const struct request *q, struct reply *r)
{
    char id[ACCOUNT_SIZE], body[640];
    const char *action = q->soap_action;

    if (strstr(action, "/AMS/LookupWbid")) {
        if (!digits_in(q, "consoleId", id))
            goto unknown;
        snprintf(body, sizeof body,
                 "<ams:LookupWbidResponse><ams:LookupWbidResult>STEAM_%s@example.com"
                 "</ams:LookupWbidResult></ams:LookupWbidResponse>",
                 id);
    } else if (strstr(action, "/AMS/GetSubscriptionInformation")) {
        if (!digits_in(q, "consoleId", id))
            goto unknown;
        snprintf(body, sizeof body,
                 "<ams:GetSubscriptionInformationResponse><ams:GetSubscriptionInformationResult>"
                 "<ams:WbidAccountId>%s@wbplay.local</ams:WbidAccountId>"
                 "<ams:SubscriptionId>%s</ams:SubscriptionId>"
                 "<ams:AccountId>%s@wbplay.local</ams:AccountId>"
                 "<ams:Entitlements></ams:Entitlements>"
                 "</ams:GetSubscriptionInformationResult></ams:GetSubscriptionInformationResponse>",
                 id, id, id);
    } else if (strstr(action, "/AMS/ConvertThirdPartyTicketWithProduct")) {
        snprintf(body, sizeof body,
                 "<ams:ConvertThirdPartyTicketWithProductResponse>"
                 "<ams:ConvertThirdPartyTicketWithProductResult>false"
                 "</ams:ConvertThirdPartyTicketWithProductResult>"
                 "</ams:ConvertThirdPartyTicketWithProductResponse>");
    } else {
        goto unknown;
    }
    r->content_type = "text/xml; charset=utf-8";
    put_string(r, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                  "<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\""
                  " xmlns:ams=\"http://www.turbine.com/SE/AMS\"><soap:Header/><soap:Body>");
    put_string(r, body);
    put_string(r, "</soap:Body></soap:Envelope>");
    return;
unknown:
    r->status = 404;
}

/* ---------------------------------------------------------------- entry */

int respond(struct state *state, const struct request *q, time_t now, struct reply *r)
{
    const char *route = q->path;
    struct state before = *state;
    int result = 0;

    r->status = 200;
    r->content_type = "application/x-hydra-binary";
    r->length = 0;

    if (!strncmp(route, "/CLS/", 5)) {
        soap(q, r);
    } else {
        /* /<number>/route */
        size_t digits = route[0] == '/' ? strspn(route + 1, "0123456789") : 0;
        if (digits && route[1 + digits] == '/')
            result = hydra(state, route + digits + 2, q, now, r);
        else
            r->status = 404;
    }
    if (result < 0)
        r->status = 400;
    if (r->status != 200) {
        r->length = 0;
        *state = before; /* a failed call changes nothing */
        return 0;
    }
    return result;
}

/* ---------------------------------------------------------------- state as text */

static const char STATE_HEADER[] = "# Mad Max local Scrap Crew: delete this file to start over\n";

static int append(char *out, size_t size, size_t *used, const char *text)
{
    size_t n = strlen(text);

    if (n >= size - *used)
        return 0;
    memcpy(out + *used, text, n + 1);
    *used += n;
    return 1;
}

size_t state_write(const struct state *state, char *out, size_t size)
{
    const struct {
        const char *name;
        const struct value *value;
    } fields[] = { { "display_name", &state->display_name },
                   { "dlcs", &state->dlcs },
                   { "age_group", &state->age_group } };
    char line[128];
    size_t used = 0;
    int ok;

    if (!size)
        return 0;
    ok = append(out, size, &used, STATE_HEADER);
    snprintf(line, sizeof line, "account=%s\nupdated_at=%llu\nscrap_rewarded=%llu\n",
             state->account, state->updated_at, state->scrap_rewarded);
    ok = ok && append(out, size, &used, line);
    for (size_t f = 0; ok && f < sizeof fields / sizeof *fields; f++) {
        snprintf(line, sizeof line, "%s=", fields[f].name);
        ok = append(out, size, &used, line);
        for (size_t i = 0; ok && i < fields[f].value->length; i++) {
            snprintf(line, sizeof line, "%02x", fields[f].value->bytes[i]);
            ok = append(out, size, &used, line);
        }
        ok = ok && append(out, size, &used, "\n");
    }
    return ok ? used : 0;
}

static int nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* hex digits to exactly one well-formed value, or nothing */
static void read_value(struct value *v, const char *hex, size_t length)
{
    v->length = 0;
    if (length % 2 || length / 2 > sizeof v->bytes)
        return;
    for (size_t i = 0; i < length / 2; i++) {
        int high = nibble(hex[2 * i]), low = nibble(hex[2 * i + 1]);
        if (high < 0 || low < 0)
            return;
        v->bytes[i] = (unsigned char)(high << 4 | low);
    }
    struct cursor c = { v->bytes, v->bytes + length / 2 };
    if (skip(&c, 0) && c.at == c.end)
        v->length = length / 2;
}

void state_read(struct state *state, const char *text)
{
    memset(state, 0, sizeof *state);
    while (*text) {
        size_t line = strcspn(text, "\r\n");
        size_t name = strcspn(text, "=\r\n");
        if (name < line) {
            struct text key = { text, name };
            const char *value = text + name + 1;
            size_t length = line - name - 1;
            struct value *field = stored_field(state, key);

            if (field)
                read_value(field, value, length);
            else if (is(key, "account"))
                account_of((struct text){ value, length }, state->account);
            else if (is(key, "updated_at"))
                state->updated_at = strtoull(value, NULL, 10);
            else if (is(key, "scrap_rewarded"))
                state->scrap_rewarded = strtoull(value, NULL, 10);
        }
        text += line;
        text += strspn(text, "\r\n");
    }
}
