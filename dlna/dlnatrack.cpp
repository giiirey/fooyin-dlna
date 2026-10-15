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

#include "dlnatrack.h"
#include "dlnaremotesourceprovider.h"

#include <core/track.h>

using namespace Qt::StringLiterals;

namespace Fooyin::Dlna {
namespace {
QString codecForMimeType(const QString& mimeType)
{
    const QString m = mimeType.toLower();
    if(m == u"audio/mpeg"_s || m == u"audio/mp3"_s) return u"mp3"_s;
    if(m == u"audio/flac"_s) return u"flac"_s;
    if(m == u"audio/ogg"_s || m == u"audio/ogg; codecs=opus"_s) return u"ogg"_s;
    if(m == u"audio/opus"_s) return u"opus"_s;
    if(m == u"audio/wav"_s) return u"wav"_s;
    if(m == u"audio/mp4"_s || m == u"audio/x-m4a"_s || m == u"audio/aac"_s) return u"m4a"_s;
    return {};
}

QString codecFromUrl(const QString& url)
{
    const QString ext = QUrl{url}.fileName().section(u'.', -1).toLower();
    if(ext == u"mp3"_s) return u"mp3"_s;
    if(ext == u"flac"_s) return u"flac"_s;
    if(ext == u"ogg"_s || ext == u"oga"_s) return u"ogg"_s;
    if(ext == u"opus"_s) return u"opus"_s;
    if(ext == u"wav"_s) return u"wav"_s;
    if(ext == u"m4a"_s || ext == u"aac"_s) return u"m4a"_s;
    return {};
}
} // namespace

Fooyin::Track trackForEntry(const DlnaEntry& entry, DlnaRemoteSourceProvider* provider)
{
    Fooyin::Track track{entry.res};

    // Register the resource URL so that opening this track routes through
    // DlnaStreamDevice (stall detection + byte-range resume).
    if(provider && !entry.res.isEmpty()) {
        provider->addDlnaUrl(entry.res);
    }

    if(!entry.title.isEmpty()) {
        track.setTitle(entry.title);
    }
    if(!entry.artist.isEmpty()) {
        track.setArtists({entry.artist});
    }
    if(!entry.album.isEmpty()) {
        track.setAlbum(entry.album);
        track.setAlbumArtists({entry.albumArtist.isEmpty() ? entry.artist : entry.albumArtist});
    }
    if(!entry.genre.isEmpty()) {
        track.setGenres({entry.genre});
    }
    if(!entry.date.isEmpty()) {
        track.setDate(entry.date);
    }
    if(!entry.trackNumber.isEmpty()) {
        track.setTrackNumber(entry.trackNumber);
    }

    if(entry.durationMs > 0) {
        track.setDuration(static_cast<uint64_t>(entry.durationMs));
    }
    if(entry.size > 0) {
        track.setFileSize(entry.size);
    }
    if(entry.bitrate > 0) {
        track.setBitrate(entry.bitrate);
    }
    if(entry.sampleRate > 0) {
        track.setSampleRate(entry.sampleRate);
    }
    if(entry.channels > 0) {
        track.setChannels(entry.channels);
    }

    if(const QString codec = !entry.mimeType.isEmpty() ? codecForMimeType(entry.mimeType)
                                                       : codecFromUrl(entry.res);
       !codec.isEmpty()) {
        track.setCodec(codec);
    }

    if(!entry.albumArt.isEmpty()) {
        track.replaceExtraTag(u"COVERART"_s, entry.albumArt);
    }

    track.setMetadataWasRead(true);
    track.setExtraProperty(u"_DLNA_OBJECTID"_s, entry.id);
    track.setExtraProperty(u"_DLNA_RES_URL"_s, entry.res);
    track.setExtraProperty(u"_DLNA_SERVER"_s, entry.serverBase);

    return track;
}
} // namespace Fooyin::Dlna
