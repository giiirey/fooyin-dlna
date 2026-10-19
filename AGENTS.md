# AGENTS.md

fooyin 0.13.x plugin (C++20, Qt 6, CMake) that browses DLNA/UPnP servers and plays their streams. Single artifact: `build/dlna/fyplugin_dlna.so`. It is not a standalone app — it only runs inside fooyin.

## Build

- Requires fooyin built **from source and installed with headers + CMake config** (`-DINSTALL_HEADERS=ON`); the `find_package(Fooyin)` in `dlna/CMakeLists.txt` provides the `create_fooyin_plugin` macro. A distro package without headers is not enough.
- Qt 6 (Core, Network, Widgets) required.
- ```sh
  cmake -B build
  cmake --build build
  ```
- Plugin metadata lives in `dlna/dlna.json.in`; `create_fooyin_plugin` templates it to `build/dlna/dlna.json`. Don't hand-edit the built JSON.

## Verifying changes (no tests in-repo)

- There is no CTest, lint, or typecheck setup. The README's "integration test" is manual: run against a real DLNA server (tested with MiniDLNA 1.3.0).
- Manual loop: copy/symlink `build/dlna/fyplugin_dlna.so` to `~/.local/lib/fooyin/plugins/`, restart fooyin, open **View → DLNA Browser**, then connect to a server by `host:port` and try browsing + Play/Add/Queue.

## CI / release constraints

- CI (`.github/workflows/build.yml`) builds on `debian:bookworm` against fooyin `v0.13.1` — the oldest supported baseline — because the released `.so` must stay forward-compatible with newer distros (Qt 6 / glibc / libstdc++). Don't build against a newer baseline for releases.
- Releases publish on tags `v*.*.*` only (see workflow).

## Architecture notes

- All plugin code is in `dlna/` (flat). Entry point: `dlnaplugin.cpp` — registers the `Dlna.ShowDialog` View-menu action and hooks `DlnaRemoteSourceProvider` into fooyin's audio loader.
- Streaming: only URLs known to be DLNA resources are routed to `DlnaStreamDevice` (custom `QIODevice` with stall detection + byte-range resume); everything else falls through to fooyin's stock `RemoteIoService`. The DLNA URL set is persisted at `Utils::statePath("dlna")/dlna_urls.json` and reloaded on startup — keep `addDlnaUrl`/`removeDlnaUrl` in sync with how tracks are handed to fooyin.
- `dlnabrowse.cpp`: SOAP Browse + DIDL-Lite parsing. Quirk: some servers (MiniDLNA) embed DIDL as an entity-escaped text node inside `<Result>` rather than nested XML — it must be unescaped before parsing (see `unescapeDidl`). Don't "simplify" this.
- `dlnadevice.cpp`: device description URL discovery tries several candidates (`description.xml`, `rootDesc.xml`, `root.xml`, `device.xml`, `rootDesc`, server root) because servers are inconsistent — keep the fallback chain when changing URL handling.
