# CWB

[CES](https://github.com/fcecin/ces) Web Browser: a portable Qt client to render content from and to interact with CES servers.

CWB is experimental software. There is no release process. The CWB codebase is almost entirely machine-generated (Claude, GenAI).

## Why?

CES servers do not natively speak either TCP or QUIC. Instead, CES servers speak two custom protocols:

* UDP/MINX/CES: a pure unreliable-datagram, packet-oriented protocol;
* UDP/MINX/RUDP/CesPlex: a custom reliable-and-ordered stream protocol that is optimized for low-cost handshaking, metering, predictability, flexibility, programmability, and ease-of-administration, not efficiency or effectiveness.

It's virtually impossible to integrate these protocols with existing web browsers.

In addition, a CES client needs to integrate a RandomX miner to produce MINX PoW tickets and CES credits. Again, integrating RandomX into existing web browsers is a painful task.

Therefore, creating a custom browser from scratch turns out to be the best option. At some point CWB will be remotely as good as say the OG Mosaic or Netscape. It's 1996 all over again!

Bonus, everything is a library now and HTML is ancient tech at this point so the GenAI can just steal browser technology until we have something remotely decent here.

## Build

Requires CMake 3.28+, a C++20 compiler, and Qt 6 (Widgets). Everything else
(litehtml, the CES client engine, CLI11) is fetched by CMake.

```
./build.sh              # -> build/cwb
./browse                # open a window, return to the shell
./browse file://<server>/s/   # browse a server's public files
```

## Addresses

No port needed: `ces://` assumes 53830 (the CES main port), the CesPlex schemes
assume 53831 (the conventional rpc port).

```
file://host/path          a stored file, rendered or downloaded
compute://host/s/app.lua  running instances of a program
lua://pid@host/           an instance, relayed by the server
luarpc://host:port/       an instance, dialed directly at its own port;
                          no port opens the server's public instance catalog
ces://host/               the server capability directory
ces://host/account        this browser's account on that server
ces://host/account/pubkey another account by public key
ces://host/apps           discover and open the live application directory
```

The first run generates the browser's identity key in the per-user app data
dir; content fetches are signed with it and pay CES fees from its account.

## Headless verbs

`./run cwb --help` lists them: `fetch` / `filefetch` / `computefetch` /
`accountfetch` print raw output curl-style, `go <url>` opens a window at an
address, `dial` streams a duplex session, `windowshot` captures the whole
window to a PNG. `./ces` and `./cesh` run the CES server and CLI fetched by
the build, so a local test server needs nothing beyond this repo.

## Tests

```
ctest --test-dir build
```

## License

Public domain (The Unlicense)
