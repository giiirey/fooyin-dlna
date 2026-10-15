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

#include <QDateTime>
#include <QString>

namespace Fooyin::Dlna {
/*!
 * A single entry (container or item) returned by a DLNA ContentDirectory Browse.
 *
 * Containers represent navigable folders (root, music, album, artist, ...).
 * Items represent playable media (music tracks, videos, ...).
 */
struct DlnaEntry
{
    //! UPnP object id (e.g. "1" or "1$FF0$64$1$1$1$14").
    QString id;
    QString parentId;
    QString title;
    //! upnp:class, e.g. "object.container.storageFolder" or "object.item.audioItem.musicTrack".
    QString upnpClass;
    //! Number of children reported by the server (for containers).
    int childCount{0};
    bool isContainer{false};

    // Item metadata.
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    QString date;
    QString trackNumber;
    //! Absolute (or server-relative) resource URL.
    QString res;
    //! Base URL of the DLNA server, e.g. "http://192.168.1.1:8200".
    QString serverBase;
    //! Raw protocolInfo, e.g. "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;...".
    QString protocolInfo;
    //! MIME type derived from protocolInfo, e.g. "audio/mpeg".
    QString mimeType;
    qint64 size{0};
    qint64 durationMs{0};
    int bitrate{0};
    int sampleRate{0};
    int channels{0};
    //! Album art URI (upnp:albumArtURI), may be empty.
    QString albumArt;

    bool playable() const
    {
        return !isContainer && !res.isEmpty();
    }
};

using DlnaEntryList = std::vector<DlnaEntry>;
} // namespace Fooyin::Dlna
