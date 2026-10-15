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

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>

namespace Fooyin::Dlna {
/*!
 * Result of resolving a DLNA media server: the base URL and the
 * ContentDirectory service control URL used for Browse requests.
 */
struct DlnaDeviceInfo
{
    bool valid{false};
    QString friendlyName;
    QString base;        // e.g. "http://192.168.1.1:8200"
    QString controlUrl;  // e.g. "http://192.168.1.1:8200/ctl/ContentDir"
    QString error;
};

/*!
 * Resolves a user-supplied host:port (or base URL) into a usable DLNA
 * ContentDirectory endpoint by fetching the root device description.
 *
 * Some servers (e.g. KeeneticOS / MiniDLNA) serve the description at
 * /rootDesc.xml rather than the root path, so a few common locations are
 * tried in order.
 */
class DlnaDevice : public QObject
{
    Q_OBJECT

public:
    DlnaDevice(QNetworkAccessManager* network, QObject* parent);

    //! Resolves @p hostPort or @p baseUrl asynchronously.
    void resolve(const QString& hostPort);
    //! Aborts any in-flight description fetch.
    void reset();

    //! Normalises user input ("192.168.1.1:8200", "http://...", "http://.../") to a base URL.
    static QString normaliseBaseUrl(const QString& input);
    static DlnaDeviceInfo parseDescription(const QByteArray& xml, const QUrl& baseUrl);

Q_SIGNALS:
    void resolved(const DlnaDeviceInfo& info);

private:
    void tryNextCandidate();
    void handleReply(QNetworkReply* reply, const QUrl& url);

    QNetworkAccessManager* m_network;
    QNetworkReply* m_pending;
    QUrl m_baseUrl;
    DlnaDeviceInfo m_info;
    int m_candidateIndex;
    static constexpr int MaxCandidates = 6;
    QUrl m_candidates[MaxCandidates];
};
} // namespace Fooyin::Dlna
