# Online calls

Observed in game against [madmax-2015-offline](https://github.com/3ek/madmax-2015-offline),
whose replies the game accepted. The replies are therefore that emulator's, not the original
servers'. (?) Which calls and which reply fields the game really needs.

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

(?) The emulator's documentation says the identifier varies between players and sessions, so a
server should match the rest of the path.

SOAP paths (v): `/CLS/WbAccountManagement.asmx`, `/CLS/WbSubscriptionManagement.asmx`,
`/CLS/AmsAuthentication.asmx`, with a `SOAPAction` header and `text/xml`.

Hydra requests send `Content-Type` and `Accept` `application/x-hydra-binary`, and
`x-http-sdk-version: 2.5.0`. The accepted replies carried `Content-Type` and
`X-Hydra-Error-Code: 200`.

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

## The reward timestamp

The profile field `timestamp_scrap_rewarded` drives the reward.

- On `profile/get` the server answers with text: `2147483647 - seconds since the last reward`,
  or `"0"` if there never was one. The game subtracts it from `2147483647` (v), so `"0"` means
  the longest possible wait.
- On `profile/update` the game sends the UTC time as text, `YYYYMMDDHHmmss` (v). The emulator
  stores its own current time instead.
- Tried in game: answering `"0"` led to Scrap Crew resources right after a save load.
