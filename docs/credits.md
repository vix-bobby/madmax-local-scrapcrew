# What comes from other projects

## madmax-2015-offline

[madmax-2015-offline](https://github.com/3ek/madmax-2015-offline), by its contributors,
GPL-3.0-or-later, commit `fee927e`.

Taken from it so far, as knowledge, learned from its source and documentation and from the game's
traffic with it:

- the hosts and request paths to answer
- the shape of the replies the game accepts, described in [network.md](network.md)
- the rule for `timestamp_scrap_rewarded`: the value sent back, and the server's own time stored
  on update

Code that follows it, rewritten in C. A file holding such code says so at its top:

| File | What follows the emulator |
|------|---------------------------|
| `src/responder.c` | profile, feed and SOAP replies; the reward timestamp rule |

The listener, the proxy and the redirection are this project's own.

This project is licensed GPL-3.0-or-later, like the emulator.
