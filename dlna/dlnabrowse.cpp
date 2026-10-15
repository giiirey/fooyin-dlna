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

#include "dlnabrowse.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QXmlStreamReader>

using namespace Qt::StringLiterals;

namespace Fooyin::Dlna {
namespace {
QString localName(const QXmlStreamReader& parser)
{
    const QStringView ref = parser.name();
    const int colon = ref.toString().indexOf(u':');
    return colon < 0 ? ref.toString() : ref.toString().mid(colon + 1);
}

QString attr(const QXmlStreamReader& parser, const QString& name)
{
    return parser.attributes().value(name).toString();
}

// The DIDL-Lite payload may be embedded inside <Result> either as nested XML
// elements, or as a single text node with XML entities (MiniDLNA does the
// latter). In the text case the raw content still contains the escape
// sequences (&lt; &gt; &amp; ...), so it must be unescaped before it can be
// parsed as a DIDL document.
QByteArray unescapeDidl(const QByteArray& rawInner)
{
    int i = 0;
    while(i < rawInner.size()) {
        const char c = rawInner.at(i);
        if(c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        break;
    }
    if(i >= rawInner.size() || rawInner.at(i) == '<') {
        return rawInner; // Empty or nested XML: parse as-is.
    }
    // Escaped text: read it back through a wrapper element, which decodes the
    // entity references.
    QXmlStreamReader unesc(QByteArray{"<w>"} + rawInner + QByteArray{"</w>"});
    while(!unesc.atEnd() && !unesc.isStartElement()) {
        unesc.readNext();
    }
    return unesc.readElementText().toUtf8();
}

QString mimeTypeFromProtocolInfo(const QString& protocolInfo)
{
    // Format: protocol:connectiontype:mimetype[:parameters]
    const QStringList parts = protocolInfo.split(u':');
    return parts.size() >= 3 ? parts.at(2) : QString();
}

QString mimeTypeFromUrl(const QString& url)
{
    const QUrl u{url};
    const QString ext = u.fileName().section(u'.', -1).toLower();
    if(ext == u"mp3"_s) return u"audio/mpeg"_s;
    if(ext == u"flac"_s) return u"audio/flac"_s;
    if(ext == u"ogg"_s || ext == u"oga"_s) return u"audio/ogg"_s;
    if(ext == u"opus"_s) return u"audio/opus"_s;
    if(ext == u"wav"_s) return u"audio/wav"_s;
    if(ext == u"m4a"_s || ext == u"aac"_s) return u"audio/mp4"_s;
    return {};
}
} // namespace

qint64 DlnaBrowse::parseDuration(const QString& duration)
{
    const QString cleaned = duration.trimmed();
    if(cleaned.isEmpty()) {
        return 0;
    }
    const QStringList parts = cleaned.split(u':');
    double seconds = 0.0;
    double mult = 1.0;
    for(int i = parts.size() - 1; i >= 0; --i) {
        double val = 0.0;
        bool ok = false;
        val = parts.at(i).trimmed().toDouble(&ok);
        if(!ok) {
            val = 0.0;
        }
        seconds += val * mult;
        mult *= 60.0;
    }
    return static_cast<qint64>(seconds * 1000.0);
}

DlnaEntryList DlnaBrowse::parseDidl(const QByteArray& didl)
{
    DlnaEntryList entries;

    QXmlStreamReader parser(didl);
    DlnaEntry* current = nullptr;

    while(!parser.atEnd()) {
        parser.readNext();

        if(parser.isStartElement()) {
            const QString ln = localName(parser);
            if(ln == u"container"_s || ln == u"item"_s) {
                DlnaEntry entry;
                entry.id = attr(parser, u"id"_s);
                entry.parentId = attr(parser, u"parentID"_s);
                entry.childCount = attr(parser, u"childCount"_s).toInt();
                entry.isContainer = (ln == u"container"_s);
                current = &entries.emplace_back(std::move(entry));
            }
            else if(current) {
                if(ln == u"title"_s) {
                    current->title = parser.readElementText();
                }
                else if(ln == u"class"_s) {
                    current->upnpClass = parser.readElementText();
                }
                else if(ln == u"artist"_s) {
                    current->artist = parser.readElementText();
                }
                else if(ln == u"creator"_s) {
                    if(current->artist.isEmpty()) {
                        current->artist = parser.readElementText();
                    }
                    else {
                        parser.readElementText();
                    }
                }
                else if(ln == u"albumArtist"_s) {
                    current->albumArtist = parser.readElementText();
                }
                else if(ln == u"album"_s) {
                    current->album = parser.readElementText();
                }
                else if(ln == u"genre"_s) {
                    current->genre = parser.readElementText();
                }
                else if(ln == u"date"_s) {
                    current->date = parser.readElementText();
                }
                else if(ln == u"originalTrackNumber"_s) {
                    current->trackNumber = parser.readElementText();
                }
                else if(ln == u"res"_s) {
                    current->size = attr(parser, u"size"_s).toLongLong();
                    current->durationMs = parseDuration(attr(parser, u"duration"_s));
                    current->bitrate = attr(parser, u"bitrate"_s).toInt();
                    current->sampleRate = attr(parser, u"sampleFrequency"_s).toInt();
                    current->channels = attr(parser, u"nrAudioChannels"_s).toInt();
                    current->protocolInfo = attr(parser, u"protocolInfo"_s);
                    current->res = parser.readElementText();
                    current->mimeType = mimeTypeFromProtocolInfo(current->protocolInfo);
                    if(current->mimeType.isEmpty()) {
                        current->mimeType = mimeTypeFromUrl(current->res);
                    }
                }
                else if(ln == u"albumArtURI"_s) {
                    current->albumArt = parser.readElementText();
                }
            }
        }
        else if(parser.isEndElement()) {
            if(parser.name() == u"container"_s || parser.name() == u"item"_s) {
                current = nullptr;
            }
        }
    }

    return entries;
}

DlnaBrowse::DlnaBrowse(QNetworkAccessManager* network, const QUrl& controlUrl, QObject* parent)
    : QObject{parent}
    , m_network{network}
    , m_controlUrl{controlUrl}
{ }

void DlnaBrowse::loadChildren(const QString& objectId, ChildrenCallback callback)
{
    m_objectId = objectId;
    m_acc.clear();
    m_callback = std::move(callback);
    fetchPage(objectId, 0, PageSize, m_acc);
}

void DlnaBrowse::fetchPage(const QString& objectId, int startIndex, int count, DlnaEntryList& acc)
{
    if(m_pending) {
        QNetworkReply* old = m_pending;
        // Clear m_pending before aborting: abort() can deliver finished
        // synchronously, and handlePage() must treat the reply as
        // superseded (m_pending != reply) so it neither resets state nor
        // re-enters while we still hold the pointer.
        m_pending = nullptr;
        old->abort();
        old->deleteLater();
    }

    const QString escaped = objectId.toHtmlEscaped();
    const QByteArray body =
        (u"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         u"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
         u"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/envelope/encoding/\">"
         u"<s:Body>"
         u"<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
         u"<ObjectID>" +
         escaped.toUtf8() +
         u"</ObjectID>"
         u"<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
         u"<Filter>*</Filter>"
         u"<StartingIndex>" +
         QString::number(startIndex).toUtf8() +
         u"</StartingIndex>"
         u"<RequestedCount>" +
         QString::number(count).toUtf8() +
         u"</RequestedCount>"
         u"<SortCriteria></SortCriteria>"
         u"</u:Browse>"
         u"</s:Body>"
         u"</s:Envelope>"_s).toUtf8();

    QNetworkRequest request{m_controlUrl};
    request.setHeader(QNetworkRequest::ContentTypeHeader, u"text/xml; charset=utf-8"_s);
    request.setRawHeader("SOAPAction",
                         "urn:schemas-upnp-org:service:ContentDirectory:1#Browse");
    request.setHeader(QNetworkRequest::UserAgentHeader, u"fooyin-dlna"_s);

    m_pending = m_network->sendCustomRequest(request, u"POST"_s.toUtf8(), body);
    QNetworkReply* reply = m_pending;
    connect(reply, &QNetworkReply::finished, this,
            [this, startIndex, count, &acc, objectId, reply]() { handlePage(reply, objectId, count, acc); });
}

void DlnaBrowse::handlePage(QNetworkReply* reply, const QString& objectId, int count, DlnaEntryList& acc)
{
    if(m_pending != reply) {
        return;
    }
    m_pending = nullptr;

    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool networkOk = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();

    if(!networkOk || httpStatus != 200) {
        const QString error = reply->error() == QNetworkReply::OperationCanceledError
                                  ? u"Browse cancelled"_s
                                  : u"HTTP %1"_s.arg(httpStatus);
        // Swap the callback out before invoking it (see the final-page path
        // below for the re-entrancy rationale).
        auto callback = std::move(m_callback);
        m_callback = {};
        if(callback) {
            callback({}, false, error);
        }
        return;
    }

    QXmlStreamReader parser(data);
    QByteArray didl;
    int numberReturned = 0;
    int totalMatches = 0;
    while(!parser.atEnd()) {
        parser.readNext();
        if(parser.isStartElement()) {
            const QString ln = localName(parser);
            if(ln == u"Result"_s) {
                // Extract the raw content of <Result>. characterOffset()
                // points just past the start tag here, and a BrowseResponse
                // contains exactly one Result element, so the closing tag can
                // be located directly in the raw data.
                const int innerStart = static_cast<int>(parser.characterOffset());
                const int innerEnd = data.lastIndexOf("</Result>");
                const QByteArray rawInner =
                    (innerEnd > innerStart)
                        ? data.mid(innerStart, innerEnd - innerStart)
                        : QByteArray{};
                didl = unescapeDidl(rawInner);
            }
            else if(ln == u"NumberReturned"_s) {
                numberReturned = parser.readElementText().trimmed().toInt();
            }
            else if(ln == u"TotalMatches"_s) {
                totalMatches = parser.readElementText().trimmed().toInt();
            }
        }
    }

    DlnaEntryList page = parseDidl(didl);
    const int entriesBefore = static_cast<int>(acc.size());
    acc.insert(acc.end(), std::make_move_iterator(page.begin()), std::make_move_iterator(page.end()));

    // Stop paginating if the server claims more matches but the page added
    // no entries: without this guard, a page that parses to nothing (e.g. a
    // DIDL the parser cannot handle) would be requested forever.
    const bool more = numberReturned > 0 && totalMatches > 0
                      && static_cast<int>(acc.size()) < totalMatches
                      && static_cast<int>(acc.size()) > entriesBefore;
    if(more) {
        fetchPage(objectId, static_cast<int>(acc.size()), count, acc);
        return;
    }

    // Swap the callback out before invoking it: the callback may re-entrantly
    // start a new browse (the dialog expands the first folders as soon as
    // the root list arrives), which replaces m_callback. Clearing it after
    // the call would wipe the new callback, and its reply would then invoke
    // an empty std::function (std::bad_function_call crash).
    auto callback = std::move(m_callback);
    m_callback = {};
    if(callback) {
        callback(acc, true, {});
    }
}
} // namespace Fooyin::Dlna
