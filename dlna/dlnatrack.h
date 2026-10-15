/*
 * fooyin-dlna
 *
 * Copyright © 2026
 *
 * This file is part of fooyin-dlna, a DLNA/UPnP browser plugin for fooyin.
 *
 * fooyin-dlna is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * fooyin-dlna is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with fooyin-dlna.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "dlnaentry.h"

#include <core/track.h>

namespace Fooyin::Dlna {
class DlnaRemoteSourceProvider;

/*!
 * Converts a DLNA item (DlnaEntry) into a fooyin Track.
 *
 * The Track's filepath is set to the item's resource URL (http(s)),
 * so fooyin's built-in ffmpeg-based audio loader can decode it natively.
 * Metadata (title, artist, album, length, bitrate, cover art, ...) is
 * populated from the DIDL-Lite item so the track is immediately usable
 * in a playlist without any local scan.
 *
 * If @p provider is non-null, the item's resource URL is registered as a
 * DLNA stream so that fooyin's audio loader routes it through
 * DlnaStreamDevice (stall detection + byte-range resume).
 */
Fooyin::Track trackForEntry(const DlnaEntry& entry, DlnaRemoteSourceProvider* provider = nullptr);
} // namespace Fooyin::Dlna
