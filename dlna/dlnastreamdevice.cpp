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

#include "dlnastreamdevice.h"

#include <core/network/networkutils.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <utility>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(DLNA_STREAM, "fy.dlna.stream")

namespace Fooyin::Dlna {
namespace {
constexpr qsizetype MinimumBufferedBytes = 64 * 1024;
} // namespace

struct DlnaStreamDeviceState
{
    QPointer<QNetworkReply> reply;
    std::mutex mutex;
    std::condition_variable_any ready;
    QString error;
    bool finished{false};
    bool aborted{false};
    bool cleanFinish{false};
    // The engine has fully received the response body of state->reply.
    // More bytes can still move from the reply's internal buffer into
    // bufferData; the stream is only over once that buffer is drained.
    bool responseComplete{false};
    // A startDlnaRequest call is queued or in flight and has not yet
    // assigned state->reply. Distinguishes "request pending" from
    // "request lost" while readData() waits.
    bool pendingRequest{false};
    std::stop_token readCancellationToken;

    QByteArray bufferData;
    qsizetype maxBufferBytes{0};
    // Bytes handed to the decoder so far (the decoder's file position).
    quint64 bytesDelivered{0};
    // Bytes the server has sent in the current response, including bytes
    // still sitting in bufferData. Combined with responseStart this is the
    // correct byte-range resume offset: data buffered but not yet consumed
    // must not be re-requested.
    quint64 replyDelivered{0};
    // File offset the current response started at. Zero for the initial
    // request and after a 200 response to a range request (the server
    // ignored the range and resends from the start).
    quint64 responseStart{0};
    quint64 bytesToSkip{0};

    int resumeAttempts{0};
    // Mirror of DlnaStreamDevice::m_maxResumeAttempts; the reply signal
    // handlers run on the network thread with only shared state in scope.
    int maxResumeAttempts{3};
    // Total file size as reported by the server (Content-Length of a 200
    // response, or the /<total> part of the Content-Range of a 206). Zero
    // when unknown (e.g. chunked transfer), in which case a mid-file close
    // cannot be detected.
    quint64 totalSize{0};
    // A response ended before the end of the file and a range-resume
    // request is queued or in flight.
    bool midFileResumePending{false};
    std::chrono::steady_clock::time_point lastByteTime{};

    bool nonBlockingReads{false};
    bool readWouldBlock{false};
    quint64 readWouldBlockCount{0};

    void resetForOpen()
    {
        error.clear();
        finished          = false;
        aborted           = false;
        cleanFinish       = false;
        responseComplete  = false;
        pendingRequest    = false;
        readCancellationToken = {};
        bufferData.clear();
        bytesDelivered  = 0;
        replyDelivered  = 0;
        responseStart   = 0;
        bytesToSkip     = 0;
        resumeAttempts    = 0;
        totalSize         = 0;
        midFileResumePending = false;
        lastByteTime      = std::chrono::steady_clock::now();
        nonBlockingReads  = false;
        readWouldBlock    = false;
        readWouldBlockCount = 0;
        reply.clear();
    }
};

namespace {
// Drains the reply's internal buffer into the device buffer, looping until
// the device buffer is full or the reply has no more data to give. Qt's
// reply may buffer up to its read buffer size beyond what a single read()
// call moves, so a single read per event would leave data behind and lose
// it at EOF. Called with state->mutex held.
// Returns true when the reply is fully drained (it can deliver no more).
bool drainReplyData(DlnaStreamDeviceState& state, QNetworkReply* reply)
{
    if(reply != state.reply || state.aborted) {
        return false;
    }

    for(;;) {
        const qsizetype availableBytes = state.maxBufferBytes - static_cast<qsizetype>(state.bufferData.size());
        if(availableBytes <= 0) {
            return false; // Buffer full; the reader will pull more.
        }

        const QByteArray data = reply->read(static_cast<qint64>(availableBytes));
        if(data.isEmpty()) {
            return true; // Reply exhausted.
        }

        state.replyDelivered += static_cast<quint64>(data.size());
        state.lastByteTime = std::chrono::steady_clock::now();

        qCDebug(DLNA_STREAM)
            << "drain:" << static_cast<void*>(reply) << "chunk" << data.size() << "replyDelivered"
            << state.replyDelivered << "buffer" << state.bufferData.size() << "skip"
            << state.bytesToSkip;

        if(state.bytesToSkip > 0) {
            const quint64 skip = std::min<quint64>(state.bytesToSkip, static_cast<quint64>(data.size()));
            state.bytesToSkip -= skip;
            if(skip < static_cast<quint64>(data.size())) {
                state.bufferData.append(data.mid(static_cast<int>(skip)));
            }
            continue;
        }

        state.bufferData.append(data);
    }
}

// Drains the reply and, once the response is complete and fully drained,
// marks the stream finished. Called with state->mutex held, from the
// network thread only.
void drainAndFinalize(DlnaStreamDeviceState& state, QNetworkReply* reply)
{
    if(state.responseComplete) {
        const bool drained = drainReplyData(state, reply);
        // The stream is terminal when the response is complete and the
        // reply can no longer deliver bytes, even if the loop stopped
        // early because the device buffer filled up mid-drain.
        if((drained || reply->bytesAvailable() == 0) && state.error.isEmpty() && !state.aborted) {
            state.finished = true;
        }
    }
    else {
        drainReplyData(state, reply);
    }
}

// Deletes @p reply once it can no longer feed the device buffer. While the
// reader's pull chain still needs bytes from the reply's internal buffer,
// deleting it would lose those bytes, so deletion is retried after a short
// delay until the reply is fully drained, replaced, or the stream is
// terminal. Must run on the network thread.
void checkReplyDeletable(const std::shared_ptr<DlnaStreamDeviceState>& state, QNetworkReply* reply)
{
    bool deletable = false;
    {
        const std::scoped_lock lock{state->mutex};
        deletable = reply != state->reply || state->aborted || state->finished || !state->error.isEmpty()
            || reply->bytesAvailable() == 0;
    }
    if(deletable) {
        reply->deleteLater();
    }
    else {
        QThread::msleep(20);
        QMetaObject::invokeMethod(reply, [state, reply]() { checkReplyDeletable(state, reply); },
                                  Qt::QueuedConnection);
    }
}

// Starts (or restarts) the HTTP request. Must run on the network thread;
// callers post it with QMetaObject::invokeMethod. When @p rangeStart is
// non-zero a `Range: bytes=<rangeStart>-` header is sent.
void startDlnaRequest(const std::shared_ptr<QNetworkAccessManager>& network, const QUrl& url,
                      const std::shared_ptr<DlnaStreamDeviceState>& state, quint64 rangeStart)
{
    QNetworkRequest req = makeNetworkRequest(url, NetworkRequestOption::AlwaysNetwork
                                             | NetworkRequestOption::AcceptAny
                                             | NetworkRequestOption::UserVerifiedRedirects);
    if(rangeStart > 0) {
        req.setRawHeader("Range",
                         QByteArray{"bytes="} + QByteArray::number(static_cast<qulonglong>(rangeStart))
                         + QByteArray{"-"});
    }

    auto* reply = network->get(req);
    qCDebug(DLNA_STREAM) << "start request:" << static_cast<void*>(reply) << "rangeStart"
                         << rangeStart;
    reply->setReadBufferSize(static_cast<qint64>(std::max(state->maxBufferBytes, MinimumBufferedBytes)));

    {
        const std::scoped_lock lock{state->mutex};
        if(state->aborted || state->finished || state->reply) {
            state->pendingRequest = false;
            reply->abort();
            reply->deleteLater();
            return;
        }

        state->reply          = reply;
        state->pendingRequest = false;
        state->readWouldBlock = state->nonBlockingReads;
        state->lastByteTime   = std::chrono::steady_clock::now();
        state->replyDelivered = 0;
        // This response completes when its own finished signal fires; the
        // previous response's completion must not terminate the new one.
        state->responseComplete     = false;
        state->midFileResumePending = false;
        // Provisional; metaDataChanged corrects it if the server answers a
        // range request with a plain 200 (resend from byte zero).
        state->responseStart  = rangeStart;
    }

    // Reply signals may arrive after the device is closed from the decode
    // thread. Capture only shared state here, never the device object.
    QObject::connect(reply, &QNetworkReply::redirected, reply, [state, reply](const QUrl&) {
        {
            const std::scoped_lock lock{state->mutex};
            if(reply == state->reply && !state->aborted) {
                reply->redirectAllowed();
            }
        }
    });
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [state, reply, rangeStart]() {
        {
            const std::scoped_lock lock{state->mutex};
            if(reply != state->reply || state->aborted) {
                return;
            }

            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qCDebug(DLNA_STREAM) << "metaDataChanged:" << static_cast<void*>(reply)
                                 << "status" << statusCode << "rangeStart" << rangeStart;
            if(statusCode == 416) {
                // Range not satisfiable: the requested offset is at or beyond
                // the end of the file. The transfer is effectively complete.
                state->cleanFinish = true;
                state->finished    = true;
            }
            else {
                // Determine where this response's body starts. A plain 200
                // to a range request means the server ignored the range and
                // is resending from byte zero. A 206 must state its range
                // in Content-Range (RFC 7233); when the header is missing
                // or unparseable, fall back to the requested offset. Bytes
                // the decoder already consumed but the server will resend
                // are skipped on the way into the buffer.
                quint64 bodyStart = rangeStart;
                if(statusCode == 200) {
                    bodyStart = 0;
                    // A 200 response (including one to a range request the
                    // server ignored) carries the full file size.
                    const qlonglong contentLength =
                        reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
                    if(contentLength > 0) {
                        state->totalSize =
                            std::max<quint64>(state->totalSize, static_cast<quint64>(contentLength));
                    }
                }
                else {
                    const QByteArray contentRange = reply->rawHeader("Content-Range");
                    // "bytes <start>-<end>/<size>"
                    const auto fields = contentRange.split(' ');
                    if(fields.size() >= 2 && fields.at(0) == QLatin1String("bytes")) {
                        const auto rangeParts = fields.at(1).split('/');
                        const auto bounds     = rangeParts.at(0).split('-');
                        if(bounds.size() == 2) {
                            bool ok = false;
                            const qlonglong start = bounds.at(0).toLongLong(&ok);
                            if(ok && start >= 0) {
                                bodyStart = static_cast<quint64>(start);
                            }
                        }
                        if(rangeParts.size() == 2) {
                            bool ok = false;
                            const qlonglong total = rangeParts.at(1).toLongLong(&ok);
                            if(ok && total > 0) {
                                state->totalSize =
                                    std::max<quint64>(state->totalSize, static_cast<quint64>(total));
                            }
                        }
                    }
                }
                state->responseStart = bodyStart;
                state->bytesToSkip   = state->bytesDelivered > bodyStart
                                           ? state->bytesDelivered - bodyStart
                                           : 0;
            }
        }
        state->ready.notify_all();
    });
    QObject::connect(reply, &QIODevice::readyRead, reply, [state, reply]() {
        qCDebug(DLNA_STREAM) << "readyRead:" << static_cast<void*>(reply) << "bytesAvailable"
                             << reply->bytesAvailable();
        {
            const std::scoped_lock lock{state->mutex};
            drainAndFinalize(*state, reply);
        }
        state->ready.notify_all();
    });
    QObject::connect(reply, &QNetworkReply::errorOccurred, reply, [network, url, state, reply](QNetworkReply::NetworkError) {
        bool resumeMidFile = false;
        quint64 resumeOffset = 0;
        {
            const std::scoped_lock lock{state->mutex};
            if(reply == state->reply && !state->aborted) {
                if(!state->cleanFinish) {
                    if(state->totalSize > 0 && state->responseStart + state->replyDelivered < state->totalSize
                       && state->resumeAttempts < state->maxResumeAttempts) {
                        // The server dropped the connection before the end
                        // of the file: recoverable. Carry over whatever
                        // still sits in this reply's internal buffer (we
                        // are on the network thread, so it can be drained
                        // directly) and re-request from the first byte not
                        // delivered.
                        drainReplyData(*state, reply);
                        resumeOffset = state->responseStart + state->replyDelivered;
                        state->error.clear();
                        state->reply.clear();
                        state->pendingRequest      = true;
                        state->midFileResumePending = true;
                        ++state->resumeAttempts;
                        resumeMidFile = true;
                    }
                    else {
                        if(state->error.isEmpty()) {
                            state->error = reply->errorString();
                        }
                        state->finished = true;
                    }
                }
            }
        }
        state->ready.notify_all();
        if(resumeMidFile) {
            qCWarning(DLNA_STREAM) << "Connection ended before end of file; resuming from byte"
                                   << resumeOffset << "(" << state->resumeAttempts << "/"
                                   << state->maxResumeAttempts << ")";
            startDlnaRequest(network, url, state, resumeOffset);
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, reply, [network, url, state, reply]() {
        qCDebug(DLNA_STREAM) << "finished signal:" << static_cast<void*>(reply) << "bytesAvailable"
                             << reply->bytesAvailable();
        bool resumeMidFile = false;
        quint64 resumeOffset = 0;
        {
            const std::scoped_lock lock{state->mutex};
            if(reply == state->reply) {
                // The engine has fully received the response body, but the
                // reply's internal buffer may still hold bytes the device
                // buffer has not accepted. The stream ends only once those
                // bytes have been pulled into the device buffer.
                state->responseComplete = true;

                const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if(reply->error() != QNetworkReply::NoError && !state->aborted && !state->cleanFinish
                   && state->error.isEmpty()) {
                    state->error = (statusCode == 404 || statusCode == 403)
                                       ? QCoreApplication::translate(nullptr, "The server rejected the request (HTTP %1).")
                                             .arg(statusCode)
                                       : reply->errorString();
                }

                drainAndFinalize(*state, reply);

                // The server ended the response before the end of the file
                // (e.g. MiniDLNA's 5-minute stream split): recoverable,
                // re-request from the first byte not delivered.
                if(state->finished && state->totalSize > 0
                   && state->responseStart + state->replyDelivered < state->totalSize && !state->cleanFinish
                   && !state->aborted) {
                    if(state->resumeAttempts < state->maxResumeAttempts) {
                        resumeOffset         = state->responseStart + state->replyDelivered;
                        state->finished       = false;
                        state->error.clear();
                        state->reply.clear();
                        state->pendingRequest      = true;
                        state->midFileResumePending = true;
                        ++state->resumeAttempts;
                        resumeMidFile = true;
                    }
                    else if(state->error.isEmpty()) {
                        // The resume budget is exhausted and the file is
                        // incomplete: surface an error so the decoder ends
                        // the stream instead of treating a truncated file
                        // as a clean EOF.
                        state->error = QCoreApplication::translate(
                            nullptr,
                            "The transfer was cut off before the end of the file.");
                    }
                }
            }
        }
        state->ready.notify_all();
        if(resumeMidFile) {
            qCWarning(DLNA_STREAM) << "Server ended the response before end of file; resuming from byte"
                                   << resumeOffset << "(" << state->resumeAttempts << "/"
                                   << state->maxResumeAttempts << ")";
            startDlnaRequest(network, url, state, resumeOffset);
        }
        checkReplyDeletable(state, reply);
    });
}
} // namespace

DlnaStreamDevice::DlnaStreamDevice(std::shared_ptr<QNetworkAccessManager> network, QUrl url, qsizetype maxBufferedBytes,
                                   QObject* parent)
    : QIODevice{parent}
    , m_network{std::move(network)}
    , m_url{std::move(url)}
    , m_state{std::make_shared<DlnaStreamDeviceState>()}
{
    m_state->maxBufferBytes = std::max<qsizetype>(maxBufferedBytes, 0);
    if(m_state->maxBufferBytes < static_cast<qsizetype>(MinimumBufferedBytes)) {
        m_state->maxBufferBytes = static_cast<qsizetype>(MinimumBufferedBytes);
    }
}

DlnaStreamDevice::~DlnaStreamDevice()
{
    DlnaStreamDevice::close();
}

bool DlnaStreamDevice::isSequential() const
{
    return true;
}

bool DlnaStreamDevice::readWouldBlock() const
{
    const std::scoped_lock lock{m_state->mutex};
    return m_state->nonBlockingReads && m_state->readWouldBlock;
}

qsizetype DlnaStreamDevice::bufferedByteCount() const
{
    const std::scoped_lock lock{m_state->mutex};
    return m_state->bufferData.size();
}

void DlnaStreamDevice::setNonBlockingReadsEnabled(bool enabled)
{
    {
        const std::scoped_lock lock{m_state->mutex};
        m_state->nonBlockingReads = enabled;
        if(!enabled) {
            m_state->readWouldBlock = false;
        }
    }
    m_state->ready.notify_all();
}

void DlnaStreamDevice::setReconnectOnFinishedEnabled(bool)
{
    // Reconnection after a clean finish is the stock behavior for live
    // streams. DLNA files end at EOF, so there is nothing to reconnect.
    // The idle watchdog with range-resume covers mid-stream stalls.
}

void DlnaStreamDevice::setReadCancellationToken(std::stop_token token)
{
    {
        const std::scoped_lock lock{m_state->mutex};
        m_state->readCancellationToken = std::move(token);
    }
    m_state->ready.notify_all();
}

void DlnaStreamDevice::setIdleTimeout(std::chrono::milliseconds timeout)
{
    m_idleTimeout = timeout;
}

void DlnaStreamDevice::setMaxResumeAttempts(int attempts)
{
    const int maxAttempts = std::max(0, attempts);
    m_maxResumeAttempts = maxAttempts;
    const std::scoped_lock lock{m_state->mutex};
    m_state->maxResumeAttempts = maxAttempts;
}

bool DlnaStreamDevice::open(OpenMode mode)
{
    if(isOpen()) {
        return true;
    }

    if(!m_url.isValid() || !(mode & ReadOnly)) {
        setErrorString(tr("Invalid network stream."));
        return false;
    }

    {
        const std::scoped_lock lock{m_state->mutex};
        m_state->resetForOpen();
        m_state->pendingRequest = true;
    }

    QIODevice::open(mode);

    const bool invoked = QMetaObject::invokeMethod(
        m_network.get(),
        [network = m_network, url = m_url, state = m_state]() { startDlnaRequest(network, url, state, 0); });
    if(!invoked) {
        const std::scoped_lock lock{m_state->mutex};
        m_state->pendingRequest = false;
        m_state->error          = tr("Failed to queue network request.");
        m_state->finished       = true;
        m_state->ready.notify_all();
    }
    return true;
}

void DlnaStreamDevice::close()
{
    QPointer<QNetworkReply> reply;
    {
        const std::scoped_lock lock{m_state->mutex};
        if(m_state->aborted && !m_state->reply) {
            QIODevice::close();
            return;
        }

        m_state->aborted  = true;
        m_state->finished = true;
        reply             = m_state->reply;
        m_state->reply.clear();
    }
    m_state->ready.notify_all();

    if(reply) {
        const auto stopRequest = [reply]() {
            if(reply) {
                reply->abort();
                reply->deleteLater();
            }
        };

        QMetaObject::invokeMethod(m_network.get(), stopRequest);
    }

    QIODevice::close();
}

qint64 DlnaStreamDevice::readData(char* data, qint64 maxSize)
{
    if(maxSize <= 0) {
        return 0;
    }

    if(QThread::currentThread() == m_network->thread()) {
        setErrorString(tr("Network stream cannot be read from the network thread."));
        return -1;
    }

    std::unique_lock lock{m_state->mutex};

    const std::stop_token cancellationToken = m_state->readCancellationToken;
    const auto hasReadableState             = [state = m_state, cancellationToken]() {
        return cancellationToken.stop_requested() || !state->bufferData.isEmpty() || state->finished || state->aborted
            || !state->error.isEmpty();
    };

    while(true) {
        if(cancellationToken.stop_requested()) {
            return -1;
        }

        if(!m_state->error.isEmpty()) {
            const QString error = m_state->error;
            lock.unlock();
            setErrorString(error);
            return -1;
        }

        if(!m_state->bufferData.isEmpty()) {
            m_state->readWouldBlock = false;

            const qsizetype bytesRead = std::min<qsizetype>(m_state->bufferData.size(), maxSize);
            std::memcpy(data, m_state->bufferData.constData(), bytesRead);
            m_state->bufferData.remove(0, bytesRead);
            m_state->bytesDelivered += static_cast<quint64>(bytesRead);
            qCDebug(DLNA_STREAM)
                << "consume:" << bytesRead << "bytesDelivered" << m_state->bytesDelivered
                << "buffer" << m_state->bufferData.size();

            // Pull more data while there is room in the buffer.
            const QPointer<QNetworkReply> reply = m_state->reply;
            if(reply && m_state->bufferData.size() < m_state->maxBufferBytes) {
                QMetaObject::invokeMethod(
                    m_network.get(),
                    [state = m_state, reply]() {
                        if(!reply) {
                            return;
                        }
                        {
                            const std::scoped_lock appendLock{state->mutex};
                            drainAndFinalize(*state, reply);
                        }
                        state->ready.notify_all();
                    },
                    Qt::QueuedConnection);
            }
            return bytesRead;
        }

        if(m_state->finished || m_state->aborted) {
            return 0;
        }

        // The buffer is empty and the transfer is incomplete. If upstream
        // has gone quiet, resume the transfer from the byte the server has
        // delivered (bytes still buffered for the decoder are included, so
        // they are not re-requested).
        const bool idle = (std::chrono::steady_clock::now() - m_state->lastByteTime) >= m_idleTimeout;
        if(m_state->reply && !m_state->pendingRequest && idle) {
            if(m_state->resumeAttempts < m_maxResumeAttempts) {
                const quint64 approxOffset = m_state->responseStart + m_state->replyDelivered;
                QPointer<QNetworkReply> oldReply = m_state->reply;
                m_state->reply.clear();
                m_state->pendingRequest = true;
                ++m_state->resumeAttempts;
                m_state->readWouldBlock = m_state->nonBlockingReads;

                qCWarning(DLNA_STREAM)
                    << "Upstream stalled after" << approxOffset << "bytes; resuming"
                    << "(" << m_state->resumeAttempts << "/" << m_maxResumeAttempts << ")";

                const auto resumeRequest = [network = m_network, url = m_url, state = m_state,
                                            oldReply]() {
                    // Runs on the network thread, serialized with the old
                    // reply's signals. Move whatever still sits in the old
                    // reply's internal buffer (the pull chain may not have
                    // finished) so the resume offset accounts for every
                    // byte the server delivered.
                    quint64 offset = 0;
                    {
                        const std::scoped_lock lock{state->mutex};
                        offset = state->responseStart + state->replyDelivered;
                        if(oldReply) {
                            for(;;) {
                                const qsizetype space =
                                    state->maxBufferBytes - static_cast<qsizetype>(state->bufferData.size());
                                if(space <= 0) {
                                    break;
                                }
                                const QByteArray data = oldReply->read(space);
                                if(data.isEmpty()) {
                                    break;
                                }
                                state->replyDelivered += static_cast<quint64>(data.size());
                                qCDebug(DLNA_STREAM)
                                    << "resume-drain:" << static_cast<void*>(oldReply) << "chunk"
                                    << data.size() << "buffer" << state->bufferData.size();
                                if(state->bytesToSkip > 0) {
                                    const quint64 skip =
                                        std::min<quint64>(state->bytesToSkip,
                                                          static_cast<quint64>(data.size()));
                                    state->bytesToSkip -= skip;
                                    if(skip < static_cast<quint64>(data.size())) {
                                        state->bufferData.append(data.mid(static_cast<int>(skip)));
                                    }
                                    continue;
                                }
                                state->bufferData.append(data);
                            }
                            // Bytes that did not fit in the buffer are lost
                            // with the reply; the resume must re-request
                            // them.
                            offset += static_cast<quint64>(oldReply->bytesAvailable());
                        }
                    }

                    qCWarning(DLNA_STREAM) << "Resuming with Range: bytes=" << offset << "-";

                    // The old reply's own finished signal (emitted after
                    // abort) drives its deferred deletion; do not delete it
                    // directly while its internal buffer may still hold
                    // bytes the reader has not consumed.
                    if(oldReply) {
                        oldReply->abort();
                    }
                    startDlnaRequest(network, url, state, offset);
                };
                QMetaObject::invokeMethod(m_network.get(), resumeRequest);
            }
            else {
                qCWarning(DLNA_STREAM)
                    << "Upstream still stalled after" << m_state->resumeAttempts << "resume attempts; failing stream";
                m_state->error    = tr("Upstream stopped delivering data.");
                m_state->finished = true;
            }
            continue; // Wait again for the resumed request to deliver data.
        }

        if(!m_state->reply && !m_state->pendingRequest && !m_state->midFileResumePending
           && m_state->error.isEmpty()) {
            // No request in flight and no terminal state: the start posted
            // from open() was lost. Fail instead of waiting forever.
            m_state->error    = tr("Lost stream request.");
            m_state->finished = true;
        }

        // Never wait longer than the idle window: a resumed request can
        // stall just like the original one, and the idle check above must
        // get another chance to run. In non-blocking mode, keep the short
        // poll interval the decoder expects.
        const auto waitDuration = m_state->nonBlockingReads ? RemoteStreamDevice::ReadWaitTimeout : m_idleTimeout;
        const bool hasData      = m_state->ready.wait_for(lock, cancellationToken, waitDuration, hasReadableState);

        if(!hasData && m_state->bufferData.isEmpty() && !m_state->finished && !m_state->aborted
           && m_state->error.isEmpty() && m_state->nonBlockingReads) {
            m_state->readWouldBlock = true;
            ++m_state->readWouldBlockCount;
            return -1;
        }
        // Otherwise: data arrived, a terminal state was reached, or the
        // wait window elapsed — loop back and re-evaluate.
    }
}

qint64 DlnaStreamDevice::writeData(const char*, qint64)
{
    return -1;
}
} // namespace Fooyin::Dlna
