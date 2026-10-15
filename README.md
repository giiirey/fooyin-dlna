# fooyin-dlna

A plugin for [fooyin](https://fooyin.org/) 0.13.x: browse and play audio from DLNA/UPnP servers.

## Features

- Connect to a DLNA server by `host:port` (e.g. `192.168.1.1:8200`).
- Automatic ContentDirectory service URL discovery (tries `description.xml`, `rootDesc.xml`, `root.xml`, `device.xml`, `rootDesc`, and the server root).
- Content catalog browsing: folders as a tree, tracks as a table (title, artist, album, genre, duration, format, bitrate, size).
- Pagination for large lists (500 items per request).
- Send tracks: Play (temporary playlist + start playback), Add (to the current playlist), Queue (to the queue).
- fooyin plays HTTP streams natively — no custom decoder is needed.

## Building

Requirements: CMake ≥ 3.16, Qt 6 (Core, Network, Widgets), fooyin 0.13.x installed with headers and CMake configuration.

```sh
cmake -B build
cmake --build build
```

Result: `build/dlna/fyplugin_dlna.so`.

## Installation

Copy (or symlink) the `.so` into the user plugins directory:

```sh
mkdir -p ~/.local/lib/fooyin/plugins
cp build/dlna/fyplugin_dlna.so ~/.local/lib/fooyin/plugins/
```

Restart fooyin. Menu entry: **View → DLNA Browser**.

## Structure

| File | Purpose |
|---|---|
| `dlna/dlnaplugin.{h,cpp}` | Plugin entry point, View menu item |
| `dlna/dlnabrowserdialog.{h,cpp}` | Browser window: tree + table, Play/Add/Queue |
| `dlna/dlnabrowsercontroller.{h,cpp}` | Wrapper over device/browse, signals for the UI |
| `dlna/dlnadevice.{h,cpp}` | Device resolution: description, ContentDirectory URL |
| `dlna/dlnabrowse.{h,cpp}` | SOAP Browse, DIDL-Lite parsing, pagination |
| `dlna/dlnatrack.{h,cpp}` | DLNA entry → `Fooyin::Track` conversion |
| `dlna/dlnastreamdevice.{h,cpp}` | HTTP streaming with stall detection and byte-range resume |
| `dlna/dlnaremotesourceprovider.{h,cpp}` | Routes DLNA resource URLs to `DlnaStreamDevice`, everything else to the stock `Fooyin::RemoteIoService` |
| `dlna/dlnaentry.h` | Data structures (catalog entries) |

## Tests

The integration test (`/tmp/dlnatest` during development) verifies the full cycle:
`normaliseBaseUrl → resolve → browse root → browse Music → All Music → Track`.

Tested against MiniDLNA 1.3.0 (Keenetic Giga, `192.168.1.1:8200`), 2771 tracks.

## Plans

- SSDP server discovery (M-SEARCH, `urn:schemas-upnp-org:device:MediaServer:1`).
- Persisting the server list between runs.

## License

GPL-3.0-or-later
