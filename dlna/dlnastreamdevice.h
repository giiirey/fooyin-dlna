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

#include <core/network/remotestreamdevice.h>

#include <QIODevice>
#include <QNetworkAccessManager>
#include <QUrl>

#include <chrono>
#include <memory>

namespace Fooyin::Dlna {
// Implementation detail of DlnaStreamDevice; defined in the .cpp.
struct DlnaStreamDeviceState;

/*!
 * A QIODevice that streams an HTTP resource with stall detection and byte-range resume.
 *
 * Mirrors fooyin's built-in NetworkStreamDevice (buffered reply + condition
 * variable), but adds an idle watchdog: if upstream stops delivering bytes
 * while the transfer is still incomplete — the typical symptom of a DLNA
 * server or router silently dropping a long-lived streaming connection — the
 * connection is aborted and the transfer is resumed with
 * `Range: bytes=<delivered>-`. Servers that support byte ranges (e.g.
 * MiniDLNA, which advertises `Accept-Ranges: bytes`) continue seamlessly.
 *
 * If a resume attempt cannot complete the transfer, the stream is marked as
 * an error and readData() returns -1, so the decoder ends the track with a
 * network error instead of stalling in silence forever.
 */
class DlnaStreamDevice : public QIODevice,
                         public Fooyin::RemoteStreamDevice
{
    Q_OBJECT

public:
    DlnaStreamDevice(std::shared_ptr<QNetworkAccessManager> network, QUrl url, qsizetype maxBufferedBytes,
                     QObject* parent = nullptr);
    ~DlnaStreamDevice() override;

    [[nodiscard]] bool isSequential() const override;
    [[nodiscard]] bool readWouldBlock() const override;
    [[nodiscard]] qsizetype bufferedByteCount() const override;
    void setNonBlockingReadsEnabled(bool enabled) override;
    void setReconnectOnFinishedEnabled(bool enabled) override;
    void setReadCancellationToken(std::stop_token token) override;

    bool open(OpenMode mode) override;
    void close() override;

    //! How long upstream may stay silent before a range-resume is attempted.
    void setIdleTimeout(std::chrono::milliseconds timeout);
    //! How many times a stalled transfer may be resumed before failing.
    void setMaxResumeAttempts(int attempts);

protected:
    qint64 readData(char* data, qint64 maxSize) override;
    qint64 writeData(const char* data, qint64 maxSize) override;

private:
    std::shared_ptr<QNetworkAccessManager> m_network;
    QUrl m_url;
    std::shared_ptr<DlnaStreamDeviceState> m_state;
    std::chrono::milliseconds m_idleTimeout{std::chrono::milliseconds{15000}};
    int m_maxResumeAttempts{3};
};
} // namespace Fooyin::Dlna
