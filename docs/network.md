# Online calls

Except in the two next sections, observed in game against
[madmax-2015-offline](https://github.com/3ek/madmax-2015-offline), whose replies the game
accepted. The replies are therefore that emulator's, not the original servers'. (?) Which calls
and which reply fields the game really needs.

## The original services

- WB Games has ended Mad Max's online features. Its
  [notice](https://wbgamessupport.wbgames.com/hc/en-us/articles/360058007413-Mad-Max-Status-Update)
  says Scrap Crews no longer gather materials, and that the challenges "A Penny Saved" and
  "Dividend" can no longer be completed.
- The game's own description of the Scrap Crew says scrap is collected while you are not playing,
  and that an online connection is needed to receive it.
- (v) The game honours the system's WinHTTP proxy setting, and accepted an interception proxy's
  certificate on the SOAP host. This is how its first request can be read.
- (i) Besides Scrap Crews, Hydra served news, terms of service, account linking and the
  "Opus War" leaderboards: see the class names in [executable.md](executable.md#online-classes).

## Without a replacement server

Seen in game through the proxy's log, over one launch with a save load (v):

- One request to each host, then no other: no retry in the roughly three minutes observed.
- The save load sent no Hydra request after the failed authentication.
- No Scrap Crew scrap arrives, on a save load or over time.
- Hydra: `auth/pc_steam` fails at name resolution (error 12007).
- SOAP: `cls.psn.turbine.com` still resolves and answers. `LookupWbid` got status 500, after
  which the game read the body and closed the request. `WbSubscriptionManagement` and
  `AmsAuthentication` were never called.
- That `LookupWbid` request carries the SteamID and a Steam ticket, so the game still sends them
  to whatever answers at that name.

## How the game uses WinHTTP

Seen in that same launch (v). With the calls answered by the mod, a request goes the same way, and
the game also reads the [reply headers](#reply-headers).

- Three sessions, all `WINHTTP_FLAG_ASYNC` with the default proxy setting: `Agora Hydra SDK
  (Win32)`, `WBNC SDK (WinHttp)`, and one without agent name that made no connection.
- Both hosts: port 443, `WINHTTP_FLAG_SECURE`.
- A status callback is set on each request handle (mask `0xffffffff`), one function for Hydra and
  another for SOAP, and cleared before the SOAP request was closed.
- The SOAP request sets `WINHTTP_OPTION_SECURITY_FLAGS` to `0x3100`: unknown certificate
  authority, wrong name and wrong date are ignored. The Hydra request set no option.
- Hydra adds `Accept`, `Content-Type` and `x-http-sdk-version` with
  `WinHttpAddRequestHeaders`; SOAP passes its headers to `WinHttpSendRequest`.
- The whole body goes with `WinHttpSendRequest`.
- After `SENDREQUEST_COMPLETE`, the game calls `WinHttpReceiveResponse` from the thread that
  sent the request, reads the status code, then calls `WinHttpReadData` (4096 bytes, no result
  pointer) until it gets 0. `WinHttpQueryDataAvailable` was not called.

## Hosts and requests

| Host | Protocol | `User-Agent` |
|------|----------|--------------|
| `blackjack.hydra.agoragames.com` | Hydra, binary over HTTPS; the name no longer resolves (v) | `Agora Hydra SDK (Win32)` |
| `cls.psn.turbine.com` | SOAP over HTTPS | `WBNC SDK (WinHttp)` |

Hydra paths start with a numeric identifier, then (v):

- `POST auth/pc_steam`
- `POST profile/get_by_platform_account_id`, `profile/create`, `profile/get`, `profile/update`
- `POST feed/get_items_by_channel`
- `GET feed/get_items_by_channel/TOS%5F<country>/1/1/0` and the same with `PP%5F`
- `POST event_log/create_events`

(i) The identifier names the game, not the player or the session: it stayed the same across
launches months apart, and the emulator's documentation lists it among its examples. That
documentation also says it varies, so the mod matches the rest of the path.

SOAP paths (v): `/CLS/WbAccountManagement.asmx`, `/CLS/WbSubscriptionManagement.asmx`,
`/CLS/AmsAuthentication.asmx`, with a `SOAPAction` header and `text/xml`.

Hydra requests send `Content-Type` and `Accept` `application/x-hydra-binary`, and
`x-http-sdk-version: 2.5.0`. The accepted replies carried `Content-Type` and
`X-Hydra-Error-Code: 200`.

## Reply headers

(v) After the status code, the game asks each Hydra reply for `Server`, `Date`, `Content-Type`,
`Connection`, `X-Hydra-Error-Code` and `X-Hydra-Processing-Time`. Replies without `Server` and
`Date`, with `Connection: close`, are accepted.

## Hydra values

Each value is a type byte followed by its data; integers are big-endian (v).

| Byte | Value |
|------|-------|
| `0x02` | 32-bit integer |
| `0x08` | array: 32-bit count, then values |
| `0x0C` | map: 32-bit count, then key and value pairs |
| `0x0D` | 64-bit unsigned integer |
| `0x0E` | text: 32-bit length, then bytes |

- `auth/pc_steam` answers with the text `0801` (`0e 00000004 30383031`). Later requests send it
  back as the header `x-ag-ticket` (v).
- `profile/get` and `profile/update` requests start with the account id (the SteamID64) as a
  `0x0D` value, followed by the list of field names or the map of changes (v).
- `profile/update` is acknowledged with the single byte `01` (v).

(v) With the mod's replies, the game made the same calls as with the emulator, all three SOAP
actions included. The main menu shows its online features as locked rather than an error, as it
also did with the emulator.

## What the mod keeps and answers

`src/responder.c` follows the replies above, with these choices:

- A profile exists once the game has created it; until then `get_by_platform_account_id` gets
  an empty map. Another account id starts over.
- Only the display name, the DLC list and the age group are kept. A 32-bit age group is returned
  as 64 bits (`0x0A`), as in the reply the game accepted.
- `updated_at` is the time of the last create or update. (i) The game reads it as the server's
  current time.
- Feed fields are answered with empty texts, the terms and privacy channels with an empty array.
- Only the three SOAP actions seen are answered.

## The reward timestamp

The profile field `timestamp_scrap_rewarded` drives the reward.

- On `profile/get` the server answers with text: `2147483647 - seconds since the last reward`,
  or `"0"` if there never was one. The game subtracts a received number from `2147483647` (v).
  A number of zero is a special case: the game then counts 300 minutes (v, see
  [executable.md](executable.md)), which is (i) what the answer `"0"` gives.
- On `profile/update` the game sends the UTC time as text, `YYYYMMDDHHmmss` (v). The emulator
  stores its own current time instead.
- Tried in game, with the emulator and with this mod: answering `"0"` led to Scrap Crew
  resources right after a save load, and the game then sent the reward time to record.
- Tried in game with this mod: later rewards follow on a save load, and both Scrap Crew
  challenges progress again.
