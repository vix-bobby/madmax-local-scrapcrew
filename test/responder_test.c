/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Replays the game's online calls against the responder. The requests and the expected replies
 * are exchanges the game is known to accept, with the account, name, country and tickets
 * replaced by made-up ones of the same length. Native build, with memory checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/responder.h"

static int failures;

#define CHECK(what)                                                                              \
    do {                                                                                         \
        if (!(what)) {                                                                           \
            printf("FAIL line %d: %s\n", __LINE__, #what);                                       \
            failures++;                                                                          \
        }                                                                                        \
    } while (0)

/* made-up account 76561190000000001, as text, as a number, and name "TestName1" */
#define SID "3736353631313930303030303030303031"
#define BIN "010fffff2587fc01"
#define NAME "546573744e616d6531"
#define PREFIX "/984815842354416438/"
/* 2026-08-24 11:41:20 UTC */
#define NOW ((time_t)1787571680)

#define DLCS                                                                                     \
    "08000000040e000000067269707065720e00000008726f636b73746172"                              \
    "0e00000016726f636b737461725f686f6f646f726e616d656e7431"                                     \
    "0e00000016726f636b737461725f686f6f646f726e616d656e7432"

#define MOTD_FIELDS(value)                                                                       \
    "0e0000000467756964" value                                                                   \
    "0e0000001d617474726962757465732e7375706f727465645f6c616e677561676573" value                 \
    "0e0000001c617474726962757465732e64697361626c65645f646c635f6c697374" value                   \
    "0e00000020617474726962757465732e72657175697265645f636f756e7472795f6c697374" value

#define PLATFORM_QUERY                                                                           \
    "0e00000011" SID "08000000070e00000004677569640e000000046e616d65"                            \
    "0e0000000577625f69640e0000000d77625f6163636f756e745f6964"                                   \
    "0e0000001277625f737562736372697074696f6e5f69640e00000004646c6373"                           \
    "0e000000096167655f67726f7570"

#define SCRAP_FIELD "0e0000001874696d657374616d705f73637261705f7265776172646564"
#define UPDATED_FIELD "0e0000000a757064617465645f6174"
#define GET_BOTH "0d" BIN "0800000002" UPDATED_FIELD SCRAP_FIELD

static size_t unhex(const char *hex, unsigned char *out)
{
    size_t n = strlen(hex) / 2;

    for (size_t i = 0; i < n; i++) {
        unsigned byte;
        sscanf(hex + 2 * i, "%2x", &byte);
        out[i] = (unsigned char)byte;
    }
    return n;
}

static struct state state;
static struct reply reply;

static int call(const char *method, const char *route, const char *action,
                const unsigned char *body, size_t length, time_t now)
{
    char path[256];
    struct request q = { method, path, action, body, length };
    /* an exact-size copy, so that reading past the end is caught */
    unsigned char *copy = malloc(length ? length : 1);

    snprintf(path, sizeof path, "%s%s", route[0] == '/' ? "" : PREFIX, route);
    memcpy(copy, body, length);
    q.body = copy;
    int changed = respond(&state, &q, now, &reply);
    free(copy);
    return changed;
}

static void exchange(int line, const char *method, const char *route, const char *request,
                     const char *expected, time_t now)
{
    unsigned char q[2048], r[2048];
    size_t q_length = unhex(request, q), r_length = unhex(expected, r);

    call(method, route, "", q, q_length, now);
    if (reply.status != 200 || reply.length != r_length || memcmp(reply.body, r, r_length)) {
        printf("FAIL line %d: %s: status %d, %zu bytes:", line, route, reply.status,
               reply.length);
        for (size_t i = 0; i < reply.length; i++)
            printf("%02x", reply.body[i]);
        printf("\n");
        failures++;
    }
}

#define POST(route, request, expected) exchange(__LINE__, "POST", route, request, expected, NOW)
#define POST_AT(when, route, request, expected)                                                  \
    exchange(__LINE__, "POST", route, request, expected, when)
#define GET(route, expected) exchange(__LINE__, "GET", route, "", expected, NOW)

static void test_session(void)
{
    POST("auth/pc_steam", "0e0000000430383032", "0e0000000430383031");
    POST("feed/get_items_by_channel",
         "0e0000000e4556454e545f524f434b5354415202000000010200000001040008000000010e0000000d"
         "617474726962757465732e5a5a",
         "0c000000010e0000000d617474726962757465732e5a5a0e00000000");
    POST("profile/get_by_platform_account_id", PLATFORM_QUERY, "0c00000000");
    CHECK(!state.account[0]);
    POST("profile/create",
         "0c000000060e000000096167655f67726f757002ffffffff"
         "0e00000011636f756e7472795f6f665f6f726967696e0e000000025a5a"
         "0e0000000c646973706c61795f6e616d650e00000009" NAME
         "0e0000000967616d655f6c616e670e00000002656e"
         "0e000000046e616d650e0000002b737465616d69645f" SID "5f706572736f6e615f" NAME
         "0e00000013706c6174666f726d5f6163636f756e745f69640e00000011" SID,
         "0d" BIN);
    GET("feed/get_items_by_channel/TOS%5FZZ/1/1/0", "0800000000");
    GET("feed/get_items_by_channel/PP%5FZZ/1/1/0", "0800000000");
    POST("profile/update", "0d" BIN "0c000000010e00000004646c6373" DLCS, "01");
    POST("feed/get_items_by_channel",
         "0e0000000b4d4f54445f474c4f42414c0200000001020000000f040008000000040e000000046775696"
         "40e0000001d617474726962757465732e7375706f727465645f6c616e6775616765730e0000001c6174"
         "74726962757465732e64697361626c65645f646c635f6c6973740e00000020617474726962757465732"
         "e72657175697265645f636f756e7472795f6c697374",
         "0c00000004" MOTD_FIELDS("0e00000000"));
    POST("event_log/create_events",
         "08000000010c000000010e0000000a6576656e745f6e616d650e00000009486561727462656174", "01");
    POST("profile/get_by_platform_account_id", PLATFORM_QUERY,
         "0c00000007" "0e00000004677569640d" BIN "0e000000046e616d650e00000009" NAME
         "0e0000000577625f69640e00000011" SID "0e0000000d77625f6163636f756e745f69640e00000011" SID
         "0e0000001277625f737562736372697074696f6e5f69640e00000011" SID
         "0e00000004646c6373" DLCS "0e000000096167655f67726f75700affffffffffffffff");
    POST("profile/update", "0d" BIN "0c000000010e0000000c646973706c61795f6e616d650e00000009" NAME,
         "01");
    POST("profile/update",
         "0d" BIN "0c000000010e0000001174696d657374616d705f747269676765720d000002106655313b",
         "01");

    /* never rewarded */
    POST("profile/get", GET_BOTH,
         "0c00000002" UPDATED_FIELD "0d0000126d573963c8" SCRAP_FIELD "0e0000000130");
    CHECK(!state.scrap_rewarded);
    POST("profile/get", "0d" BIN "0800000001" UPDATED_FIELD,
         "0c00000001" UPDATED_FIELD "0d0000126d573963c8");
    /* the game's own time (11:41:20) is replaced by ours (11:41:21) */
    POST_AT(NOW + 1, "profile/update",
            "0d" BIN "0c00000001" SCRAP_FIELD "0e0000000e3230323630383234313134313230", "01");
    CHECK(state.scrap_rewarded == 20260824114121ULL);
    /* 2327 seconds later */
    POST_AT(NOW + 1 + 2327, "profile/get", "0d" BIN "0800000001" SCRAP_FIELD,
            "0c00000001" SCRAP_FIELD "0e0000000a32313437343831333230");
    /* a clock set back: no time has passed */
    POST_AT(NOW - 100, "profile/get", "0d" BIN "0800000001" SCRAP_FIELD,
            "0c00000001" SCRAP_FIELD "0e0000000a32313437343833363437");
    /* across a month and a year: 2027-01-01 00:00:00 is 1798761600 */
    state.scrap_rewarded = 20261231235959ULL;
    POST_AT((time_t)1798761600, "profile/get", "0d" BIN "0800000001" SCRAP_FIELD,
            "0c00000001" SCRAP_FIELD "0e0000000a32313437343833363436");
}

/* ---------------------------------------------------------------- SOAP */

#define ENVELOPE(inner)                                                                          \
    "<?xml version=\"1.0\" encoding=\"utf-8\"?><soap:Envelope xmlns:soap=\"http://schemas."      \
    "xmlsoap.org/soap/envelope/\" xmlns:ams=\"http://www.turbine.com/SE/AMS\"><soap:Header/>"    \
    "<soap:Body>" inner "</soap:Body></soap:Envelope>"
#define ID "76561190000000001"

static void soap(int line, const char *method, const char *expected)
{
    char action[128], body[512];
    int n = snprintf(body, sizeof body,
                     "<soap:Envelope><soap:Body><ams:%s><ams:consoleTicket>TICKET"
                     "</ams:consoleTicket><ams:realm>STEAM</ams:realm><ams:consoleId>" ID
                     "</ams:consoleId></ams:%s></soap:Body></soap:Envelope>",
                     method, method);

    snprintf(action, sizeof action, "http://www.turbine.com/SE/AMS/%s", method);
    call("POST", "/CLS/WbAccountManagement.asmx", action, (unsigned char *)body, (size_t)n, NOW);
    if (reply.status != 200 || reply.length != strlen(expected)
        || memcmp(reply.body, expected, reply.length)
        || strcmp(reply.content_type, "text/xml; charset=utf-8")) {
        printf("FAIL line %d: %s: status %d: %.*s\n", line, method, reply.status,
               (int)reply.length, reply.body);
        failures++;
    }
}

static void test_soap(void)
{
    soap(__LINE__, "LookupWbid",
         ENVELOPE("<ams:LookupWbidResponse><ams:LookupWbidResult>STEAM_" ID "@example.com"
                  "</ams:LookupWbidResult></ams:LookupWbidResponse>"));
    soap(__LINE__, "GetSubscriptionInformation",
         ENVELOPE("<ams:GetSubscriptionInformationResponse>"
                  "<ams:GetSubscriptionInformationResult><ams:WbidAccountId>" ID "@wbplay.local"
                  "</ams:WbidAccountId><ams:SubscriptionId>" ID "</ams:SubscriptionId>"
                  "<ams:AccountId>" ID "@wbplay.local</ams:AccountId><ams:Entitlements>"
                  "</ams:Entitlements></ams:GetSubscriptionInformationResult>"
                  "</ams:GetSubscriptionInformationResponse>"));
    soap(__LINE__, "ConvertThirdPartyTicketWithProduct",
         ENVELOPE("<ams:ConvertThirdPartyTicketWithProductResponse>"
                  "<ams:ConvertThirdPartyTicketWithProductResult>false"
                  "</ams:ConvertThirdPartyTicketWithProductResult>"
                  "</ams:ConvertThirdPartyTicketWithProductResponse>"));
    call("POST", "/CLS/x.asmx", "http://www.turbine.com/SE/AMS/Other", (unsigned char *)"", 0, NOW);
    CHECK(reply.status == 404 && !reply.length);
    /* no console id, or one that is not a number */
    const char *bad[] = { "<ams:consoleId>", "<ams:consoleId>x</ams:consoleId>", "" };
    for (int i = 0; i < 3; i++) {
        call("POST", "/CLS/x.asmx", "http://www.turbine.com/SE/AMS/LookupWbid",
             (const unsigned char *)bad[i], strlen(bad[i]), NOW);
        CHECK(reply.status == 404);
    }
}

/* ---------------------------------------------------------------- state */

static void test_state(void)
{
    char text[STATE_TEXT_SIZE];
    struct state copy;
    size_t n = state_write(&state, text, sizeof text);

    CHECK(n && n == strlen(text) && strstr(text, "account=" ID "\n"));
    state_read(&copy, text);
    CHECK(!memcmp(&copy, &state, sizeof copy));
    for (size_t size = 0; size <= n; size++) {
        char *small = malloc(size ? size : 1);
        CHECK(state_write(&state, small, size) == 0);
        free(small);
    }
    state_read(&copy, "account=12\r\nnonsense\n=\ndlcs=0e000000ff41\ndisplay_name=zz\n"
                      "age_group=01\nunknown=1\nscrap_rewarded=20260101000000");
    CHECK(!strcmp(copy.account, "12") && !copy.dlcs.length && !copy.display_name.length);
    CHECK(copy.age_group.length == 1 && copy.scrap_rewarded == 20260101000000ULL);
    state_read(&copy, "account=1; rm\n");
    CHECK(!copy.account[0]);
    state_read(&copy, "");
    CHECK(!copy.account[0] && !copy.scrap_rewarded);

    /* another account starts over */
    CHECK(state.scrap_rewarded);
    POST("profile/get", "0d010fffff2587fc02" "0800000001" SCRAP_FIELD,
         "0c00000001" SCRAP_FIELD "0e0000000130");
    CHECK(!strcmp(state.account, "76561190000000002") && !state.display_name.length);
}

/* ---------------------------------------------------------------- bad input */

static void test_bad_input(void)
{
    const char *routes[] = { "profile/create", "profile/get_by_platform_account_id", "profile/get",
                             "profile/update", "feed/get_items_by_channel" };
    const char *seeds[] = {
        "0c000000020e000000096167655f67726f757002ffffffff"
        "0e00000013706c6174666f726d5f6163636f756e745f69640e00000011" SID,
        PLATFORM_QUERY,
        GET_BOTH,
        "0d" BIN "0c000000010e00000004646c6373" DLCS,
        "0e0000000e4556454e545f524f434b5354415202000000010200000001040008000000010e0000000141",
    };
    unsigned char body[2048];

    srand(1);
    for (int r = 0; r < 5; r++) {
        size_t n = unhex(seeds[r], body);
        for (size_t length = 0; length < n; length++) { /* every truncation is refused */
            struct state before = state;
            CHECK(!call("POST", routes[r], "", body, length, NOW));
            CHECK(reply.status == 400 && !reply.length && !memcmp(&before, &state, sizeof state));
        }
        for (int i = 0; i < 20000; i++) { /* damaged bytes must only not crash */
            unsigned char damaged[2048];
            memcpy(damaged, body, n);
            for (int k = 1 + rand() % 4; k; k--)
                damaged[(size_t)rand() % n] = (unsigned char)(rand() % 3 ? rand() : 0xff);
            call("POST", routes[r], "", damaged, n, NOW);
            CHECK(reply.length <= sizeof reply.body);
        }
    }
    /* deeply nested arrays */
    size_t n = unhex("0d" BIN "0c000000010e0000000161", body);
    while (n + 5 < sizeof body) {
        memcpy(body + n, "\x08\x00\x00\x00\x01", 5);
        n += 5;
    }
    call("POST", "profile/update", "", body, n, NOW);
    CHECK(reply.status == 400);

    const char *paths[] = { "", "/", "/x/auth/pc_steam", "/1auth/pc_steam", "/auth/pc_steam" };
    for (int i = 0; i < 5; i++) {
        struct request q = { "POST", paths[i], "", body, 0 };
        respond(&state, &q, NOW, &reply);
        CHECK(reply.status == 404);
    }
    call("POST", "unknown/route", "", body, 0, NOW);
    CHECK(reply.status == 404);
    call("GET", "auth/pc_steam", "", body, 0, NOW);
    CHECK(reply.status == 404);
}

int main(void)
{
    test_session();
    test_soap();
    test_state();
    test_bad_input();
    printf(failures ? "responder: %d FAILED\n" : "responder: all tests passed\n", failures);
    return failures != 0;
}
