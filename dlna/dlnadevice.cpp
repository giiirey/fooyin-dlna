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

#include "dlnadevice.h"

#include <QByteArray>
#include <QHostInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QXmlStreamReader>

using namespace Qt::StringLiterals;

namespace Fooyin::Dlna {
namespace {
constexpr auto ContentDirectoryType = "urn:schemas-upnp-org:service:ContentDirectory:1";
} // namespace

DlnaDevice::DlnaDevice(QNetworkAccessManager* network, QObject* parent)
    : QObject{parent}
    , m_network{network}
    , m_pending{nullptr}
    , m_candidateIndex{0}
{
    qRegisterMetaType<DlnaDeviceInfo>("DlnaDeviceInfo");
}

QString DlnaDevice::normaliseBaseUrl(const QString& input)
{
    QString s = input.trimmed();

    // Allow a bare "host" or "host:port".
    if(s.indexOf(u"://"_s) < 0) {
        s = u"http://" + s;
    }

    QUrl url{s};
    if(!url.isValid() || url.host().isEmpty()) {
        return {};
    }
    if(url.scheme().isEmpty() || url.scheme() == u"http"_s || url.scheme() == u"https"_s) {
        // Keep host:port, drop any path.
        QString base = u"%1://%2"_s.arg(url.scheme(), url.authority());
        return base;
    }
    return {};
}

DlnaDeviceInfo DlnaDevice::parseDescription(const QByteArray& xml, const QUrl& baseUrl)
{
    DlnaDeviceInfo info;
    info.base = baseUrl.toString();

    QXmlStreamReader parser(xml);
    bool inService = false;
    bool serviceTypeMatches = false;
    QString controlUrl;
    QString friendlyName;

    while(!parser.atEnd()) {
        parser.readNext();

        if(parser.isStartElement()) {
            const QStringView name = parser.name();
            if(name == u"service"_s) {
                inService = true;
                serviceTypeMatches = false;
                controlUrl.clear();
            }
            else if(inService) {
                if(name == u"serviceType"_s) {
                    const QString type = parser.readElementText();
                    serviceTypeMatches = type.endsWith(u"ContentDirectory:1"_s);
                }
                else if(name == u"controlURL"_s) {
                    controlUrl = parser.readElementText();
                }
            }
            else if(name == u"friendlyName"_s) {
                friendlyName = parser.readElementText();
            }
        }
        else if(parser.isEndElement()) {
            if(parser.name() == u"service"_s && inService) {
                inService = false;
                if(serviceTypeMatches && !controlUrl.isEmpty()) {
                    QUrl control{controlUrl};
                    if(!control.isRelative()) {
                        info.controlUrl = control.toString();
                    }
                    else {
                        info.controlUrl = baseUrl.resolved(control).toString();
                    }
                }
            }
        }
    }

    info.friendlyName = friendlyName;
    info.valid = !info.controlUrl.isEmpty();
    if(!info.valid) {
        info.error = u"ContentDirectory service not found in device description"_s;
    }
    return info;
}

void DlnaDevice::resolve(const QString& hostPort)
{
    const QString base = normaliseBaseUrl(hostPort);
    if(base.isEmpty()) {
        DlnaDeviceInfo info;
        info.error = u"Invalid address. Expected host:port, e.g. 192.168.1.1:8200"_s;
        Q_EMIT resolved(info);
        return;
    }

    m_baseUrl = QUrl{base};
    m_info = {};
    m_info.base = base;

    // Candidate description locations, in order of likelihood.
    m_candidates[0] = QUrl{base + u"/"_s};
    m_candidates[1] = QUrl{base + u"/rootDesc.xml"_s};
    m_candidates[2] = QUrl{base + u"/description.xml"_s};
    m_candidates[3] = QUrl{base + u"/rootDesc"_s};
    m_candidates[4] = QUrl{base + u"/root.xml"_s};
    m_candidates[5] = QUrl{base + u"/device.xml"_s};
    m_candidateIndex = 0;

    tryNextCandidate();
}

void DlnaDevice::tryNextCandidate()
{
    if(m_pending) {
        m_pending->deleteLater();
        m_pending = nullptr;
    }

    if(m_candidateIndex >= MaxCandidates) {
        m_info.valid = false;
        m_info.error = u"Could not fetch the UPnP device description from this server"_s;
        Q_EMIT resolved(m_info);
        return;
    }

    const QUrl url = m_candidates[m_candidateIndex++];
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, u"fooyin-dlna"_s);
    m_pending = m_network->get(request);
    // Capture the reply by value: m_pending may be replaced (or cleared by
    // reset()) before this reply finishes, and handleReply() relies on
    // comparing against the reply that actually completed.
    QNetworkReply* reply = m_pending;
    connect(reply, &QNetworkReply::finished, this,
            [this, url, reply]() { handleReply(reply, url); });
}

void DlnaDevice::reset()
{
    if(m_pending) {
        QNetworkReply* old = m_pending;
        // Clear m_pending before aborting: abort() can deliver finished
        // synchronously, and handleReply() must treat the reply as
        // superseded (m_pending != reply) so it does not re-enter
        // tryNextCandidate() while we still hold the pointer.
        m_pending = nullptr;
        old->abort();
        old->deleteLater();
    }
    m_candidateIndex = 0;
}

void DlnaDevice::handleReply(QNetworkReply* reply, const QUrl& url)
{
    if(m_pending != reply) {
        return;
    }
    m_pending = nullptr;
    reply->deleteLater();

    const DlnaDeviceInfo parsed = parseDescription(reply->readAll(), m_baseUrl);
    if(parsed.valid) {
        m_info = parsed;
        Q_EMIT resolved(m_info);
        return;
    }

    // Not a valid description here; try the next candidate.
    tryNextCandidate();
}
} // namespace Fooyin::Dlna
