# fooyin-dlna

A plugin for [fooyin](https://fooyin.org/) 0.14.x: browse and play audio from DLNA/UPnP servers.

## Features

- Connect to a DLNA server by `host:port` (e.g. `192.168.1.1:8200`).
- Automatic ContentDirectory service URL discovery (tries `description.xml`, `rootDesc.xml`, `root.xml`, `device.xml`, `rootDesc`, and the server root).
- Content catalog browsing: folders as a tree, tracks as a table (title, artist, album, genre, duration, format, bitrate, size).
- Pagination for large lists (500 items per request).
- Send tracks: Play (temporary playlist + start playback), Add (to the current playlist), Queue (to the queue).
- fooyin plays HTTP streams natively — no custom decoder is needed.

## Installation

### Prebuilt binary (recommended)

Prebuilt `fyplugin_dlna.so` for Linux x86_64 is published on the [releases page](https://github.com/giiirey/fooyin-dlna/releases). Download the latest one and put it into the plugins directory:

```sh
mkdir -p ~/.local/lib/fooyin/plugins
cp fyplugin_dlna.so ~/.local/lib/fooyin/plugins/
```

Restart fooyin. Menu entry: **View → DLNA Browser**.

No extra packages are needed: the binary links against the Qt 6 and fooyin libraries your system already has. It is built against the oldest supported fooyin/Qt baseline (Debian 12), so the same `.so` works on every distro that ships fooyin 0.14.x (Debian, Ubuntu, Fedora, Arch, openSUSE, ...).

### Build from source

Requirements: CMake ≥ 3.16, Qt 6 (Core, Network, Widgets), fooyin 0.14.x installed with headers and CMake configuration.

```sh
cmake -B build
cmake --build build
```

Then copy (or symlink) the result, `build/dlna/fyplugin_dlna.so`, into the plugins directory as above and restart fooyin.

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

The integration test during development verifies the full cycle:
`normaliseBaseUrl → resolve → browse root → browse Music → All Music → Track`.

Tested against MiniDLNA 1.3.0 (Keenetic Giga), 2771 tracks.

## Plans

- SSDP server discovery (M-SEARCH, `urn:schemas-upnp-org:device:MediaServer:1`).

## License

GPL-3.0-or-later
