# hot-update-agent

A console client whose functional part is updated **without restarting the process**,
and an update server that accepts new versions and pushes them to clients. A small,
dependency-free model of how an endpoint agent ships new logic to the field.

## Contents

1. [Quick start](#1-quick-start)
2. [What it does](#2-what-it-does)
3. [Design](#3-design)
4. [Decisions and alternatives](#4-decisions-and-alternatives)
5. [Testing](#5-testing)
6. [Limitations and what production needs](#6-limitations-and-what-production-needs)

## 1. Quick start

Requirements: CMake ≥ 3.20, Ninja, a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022).
No other dependency: GoogleTest is used if installed, otherwise fetched by CMake.

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
scripts/demo.sh            # server + client on v1, then v2 is published
```

Windows: `cmake --preset msvc && cmake --build --preset msvc && ctest --preset msvc`.

Manual run (three terminals):

```bash
build/dev/nx-server --store /tmp/nx-store
build/dev/nx-client --module build/dev/functional_v1.so --cache /tmp/nx-client
build/dev/nx-publish --version 2.0.0 --file build/dev/functional_v2.so
```

Demo output (Linux, 26/10/2026):

```
client | [host] now running 1.0.0
client | [functional] version 1.0.0
client | [agent] connected to 127.0.0.1:47811
server | [server] client linux-x86_64 running 1.0.0
publish| ok: published 2.0.0
server | [server] published 2.0.0 for linux-x86_64 (20088 bytes, sha256 6d6e41e060bf...)
client | [agent] update available: 2.0.0
client | [host] now running 2.0.0 (was 1.0.0)
client | [functional] version 2.0.0
```

## 2. What it does

| Goal | Where |
|---|---|
| Client = functional part + update part | `functional_v1/v2` (shared library) + `ModuleHost` / `UpdateAgent` |
| Functional part prints its version at launch and after an update | `nx_run()`, called by `ModuleHost::load` |
| Update without restarting the process | `ModuleHost`: loads the new library and swaps it in (§3) |
| Server accepts a new version | `PUBLISH` message, sent by `nx-publish` |
| Server sends it to the client | `UPDATE_AVAILABLE` push + `GET` / `MODULE` |
| As soon as possible | Push over a persistent connection, no polling: ~3 ms after publishing in the demo |
| Windows, macOS, Linux | One code base; platform code isolated in two places (§3) |
| No runtime dependencies | No third-party runtime code at all; static MSVC runtime on Windows |

## 3. Design

```
nx-publish ──PUBLISH──▶ nx-server ──UPDATE_AVAILABLE (push)──▶ nx-client
                          │  ◀────────────── GET ──────────────┘   │
                          └──────────── MODULE (bytes+sha256) ────▶ UpdateAgent
                                                                    │ verify sha256, write atomically
                                                                    ▼
                                                               ModuleHost::load
                                                     copy → dlopen/LoadLibrary → check ABI → swap
```

- **Functional part:** a shared library with a C ABI (`nx_abi_version`, `nx_version`,
  `nx_run`, [`include/nx/functional_api.h`](include/nx/functional_api.h)). C, not C++,
  across the boundary: client and module can be built at different times and with
  different compilers. Built twice (1.0.0 and 2.0.0) from the same source.
- **ModuleHost:** loads each version from a private copy with a unique name (on Windows a
  DLL in use cannot be overwritten; on every platform the loader would return the image
  already loaded for the same path). Validates symbols, ABI and version, then swaps a
  `shared_ptr`. A call still running on the old version finishes on it; the old library
  is unloaded when the last user releases it. Any failure keeps the current version.
- **UpdateAgent:** one thread with a persistent connection. Sends `HELLO{version,
  platform}`, waits for pushes, downloads, checks SHA-256, writes the file atomically
  (temp + rename) and hands it to the host. Reconnects with exponential backoff (cap 5 s).
  Stops within 100 ms (`std::jthread` + `stop_token`, bounded waits).
- **UpdateServer:** accept thread + one thread per connection. Keeps the latest version
  **per platform** (a Linux client never gets a DLL) and persists it to disk, so a restart
  serves the same version. Rejects versions that are not newer. Notifies every subscribed
  client of that platform when something is published. Graceful stop: closes sockets and
  joins every thread.
- **Protocol:** length-prefixed binary frames, bounds-checked decoding, 64 MiB frame cap
  (a bad peer cannot make us allocate gigabytes), fuzzed in the tests.
- **Platform code:** only `src/common/net.cpp` (POSIX sockets / Winsock) and
  `src/client/module_host.cpp` (`dlopen` / `LoadLibraryW`). Everything else is standard C++20.

## 4. Decisions and alternatives

| Decision | Alternative | Why |
|---|---|---|
| Shared library swapped in the same process | Functional part as a child process, restarted by a supervisor | The goal is not to restart the process. A child process isolates crashes better and is what I would choose in production for untrusted or crash-prone code |
| Push over a persistent TCP connection | Polling every N seconds | "As soon as possible" with zero idle traffic; polling is simpler through proxies (HTTP) |
| Own binary protocol over TCP | HTTP(S) + JSON (e.g. libcurl) | No dependency, tiny code; production would use TLS and probably HTTPS for proxies and CDNs |
| Copy-then-load with unique names | Load in place | Avoids file locks on Windows and loader caching |
| `shared_ptr` swap under a mutex | `std::atomic<std::shared_ptr>` / RCU | Same semantics; the mutex is portable to every standard library today |
| Thread per connection | Async I/O (epoll/IOCP, Asio) | Simple and enough here; thousands of clients need async I/O |
| SHA-256 for integrity | Digital signature | Integrity only; authenticity needs signatures (§6) |

## 5. Testing

20 tests (GoogleTest), all passing on Linux with GCC 15, also under ASan + UBSan and
ThreadSanitizer (26/10/2026):

- SHA-256 against NIST vectors (including 1,000,000 × "a").
- Protocol: round trips, every truncated prefix rejected, trailing bytes, unknown types,
  length fields that would overrun, 20,000 random frames (fuzz).
- Versions: numeric comparison (1.9.0 < 1.10.0), malformed strings.
- ModuleHost: swap while the old module is still in use, invalid library rejected while
  the current one keeps running, same file loaded twice.
- End to end on a free port: published version reaches a running client; a client that
  connects later gets the latest; server rejects equal or older versions; server restart
  serves the stored version; client starts without a server and catches up when it appears.

CI matrix (`.github/workflows/ci.yml`): Ubuntu (dev and ASan), macOS and Windows/MSVC.
**Only Linux was run here**; the Windows and macOS paths compile in CI.

## 6. Limitations and what production needs

1. **Authenticity:** sign modules (Ed25519) and verify with a public key embedded in the
   client; TLS (or mTLS) for the transport. Today anyone who can reach the server can publish.
2. **Publisher authentication** and an audit log.
3. **Rollback:** keep the previous module; if the new one fails a health check (crash,
   timeout, self-test), revert and report it.
4. **Staged rollout:** percentages, rings, kill switch.
5. **Crash isolation:** run the functional part in a child process if it can crash.
6. **Scale:** async I/O, CDN for the bytes, delta updates, resumable downloads, rate limits.
7. **Versioning:** ABI negotiation (`HELLO` carries the ABI; server keeps one build per ABI),
   minimum/maximum client versions.
8. **Operations:** structured logs, metrics (update latency, failures by version), config file.
9. **Minor:** finished per-connection threads are only joined at shutdown; fine here, pruned
   in production.
