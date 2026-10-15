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

#include <core/network/remoteioservice.h>
#include <core/network/remotesourceprovider.h>

#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <chrono>
#include <memory>
#include <mutex>

namespace Fooyin {
class SettingsManager;
}

namespace Fooyin::Dlna {
/*!
 * A Fooyin::RemoteSourceProvider that routes known DLNA resource URLs to
 * DlnaStreamDevice (stall detection + byte-range resume) and delegates
 * everything else to fooyin's stock Fooyin::RemoteIoService, so radio,
 * HLS, and plain HTTP behavior is preserved exactly.
 *
 * Resource URLs are registered at track-creation time (see
 * DlnaBrowserDialog) and the set is persisted, so tracks saved in
 * playlists before a restart keep working without re-browsing.
 */
class DlnaRemoteSourceProvider : public QObject,
                                 public Fooyin::RemoteSourceProvider
{
    Q_OBJECT

public:
    DlnaRemoteSourceProvider(std::shared_ptr<QNetworkAccessManager> network, SettingsManager* settings,
                             QObject* parent = nullptr);

    [[nodiscard]] Fooyin::RemoteStreamSource createStreamSource(const QUrl& url) const override;

    //! Registers @p url as a DLNA stream (thread-safe, idempotent, persisted).
    void addDlnaUrl(const QString& url);
    //! Unregisters @p url (thread-safe, persisted).
    void removeDlnaUrl(const QString& url);
    [[nodiscard]] bool isDlnaUrl(const QUrl& url) const;

    //! How long upstream may stay silent before a range-resume is attempted.
    void setIdleTimeout(std::chrono::milliseconds timeout);
    //! How many times a stalled transfer may be resumed before failing.
    void setMaxResumeAttempts(int attempts);

private:
    void loadPersistedUrls();
    void persistUrls();

    std::shared_ptr<QNetworkAccessManager> m_network;
    SettingsManager* m_settings;
    std::shared_ptr<Fooyin::RemoteIoService> m_default;

    mutable std::mutex m_mutex;
    QSet<QString> m_dlnaUrls;

    std::chrono::milliseconds m_idleTimeout{std::chrono::milliseconds{15000}};
    int m_maxResumeAttempts{3};
};
} // namespace Fooyin::Dlna
