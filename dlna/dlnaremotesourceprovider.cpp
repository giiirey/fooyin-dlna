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

#include "dlnaremotesourceprovider.h"
#include "dlnastreamdevice.h"

#include <utils/fypaths.h>
#include <utils/settings/settingsmanager.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMutex>
#include <QVariant>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(DLNA_PROVIDER, "fy.dlna.provider")

namespace Fooyin::Dlna {
namespace {
QString urlsFilePath()
{
    return Utils::statePath(u"dlna"_s) + u"/dlna_urls.json"_s;
}
} // namespace

DlnaRemoteSourceProvider::DlnaRemoteSourceProvider(std::shared_ptr<QNetworkAccessManager> network,
                                                   SettingsManager* settings, QObject* parent)
    : QObject{parent}
    , m_network{std::move(network)}
    , m_settings{settings}
    , m_default{std::make_shared<Fooyin::RemoteIoService>(m_network, settings, parent)}
{
    loadPersistedUrls();
}

Fooyin::RemoteStreamSource DlnaRemoteSourceProvider::createStreamSource(const QUrl& url) const
{
    {
        const std::scoped_lock lock{m_mutex};
        if(!m_dlnaUrls.contains(url.toString())) {
            return m_default->createStreamSource(url);
        }
    }

    int readAheadKb = 2048;
    int openTimeoutMs = 8000;
    if(m_settings) {
        const int configuredReadAheadKb   = m_settings->value(u"Engine/RemoteReadAheadKb"_s).toInt();
        const int configuredOpenTimeoutMs = m_settings->value(u"Engine/RemoteOpenTimeoutMs"_s).toInt();
        if(configuredReadAheadKb > 0) {
            readAheadKb = configuredReadAheadKb;
        }
        if(configuredOpenTimeoutMs >= 1000) {
            openTimeoutMs = configuredOpenTimeoutMs;
        }
    }
    const qsizetype maxBufferedBytes = static_cast<qsizetype>(readAheadKb) * 1024;

    auto device = std::make_unique<DlnaStreamDevice>(m_network, url, maxBufferedBytes);
    device->setIdleTimeout(m_idleTimeout);
    device->setMaxResumeAttempts(m_maxResumeAttempts);

    qCDebug(DLNA_PROVIDER) << "Opening DLNA stream with stall detection and range resume:" << url.toString();

    Fooyin::RemoteStreamSource source;
    source.remoteDevice = device.get();
    source.openTimeout  = std::chrono::milliseconds{openTimeoutMs};
    source.device       = std::move(device);
    return source;
}

void DlnaRemoteSourceProvider::addDlnaUrl(const QString& url)
{
    bool changed{false};
    {
        const std::scoped_lock lock{m_mutex};
        changed = !m_dlnaUrls.contains(url);
        m_dlnaUrls.insert(url);
    }
    if(changed) {
        persistUrls();
    }
}

void DlnaRemoteSourceProvider::removeDlnaUrl(const QString& url)
{
    bool changed{false};
    {
        const std::scoped_lock lock{m_mutex};
        changed = m_dlnaUrls.remove(url) > 0;
    }
    if(changed) {
        persistUrls();
    }
}

bool DlnaRemoteSourceProvider::isDlnaUrl(const QUrl& url) const
{
    const std::scoped_lock lock{m_mutex};
    return m_dlnaUrls.contains(url.toString());
}

void DlnaRemoteSourceProvider::setIdleTimeout(std::chrono::milliseconds timeout)
{
    m_idleTimeout = timeout;
}

void DlnaRemoteSourceProvider::setMaxResumeAttempts(int attempts)
{
    m_maxResumeAttempts = std::max(0, attempts);
}

void DlnaRemoteSourceProvider::loadPersistedUrls()
{
    QFile file{urlsFilePath()};
    if(!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if(error.error != QJsonParseError::NoError || !document.isArray()) {
        qCWarning(DLNA_PROVIDER) << "Ignoring malformed DLNA URL list:" << urlsFilePath();
        return;
    }

    const std::scoped_lock lock{m_mutex};
    for(const QJsonValue& value : document.array()) {
        if(const QString url = value.toString(); !url.isEmpty()) {
            m_dlnaUrls.insert(url);
        }
    }
    if(!m_dlnaUrls.isEmpty()) {
        qCDebug(DLNA_PROVIDER) << "Loaded" << m_dlnaUrls.size() << "persisted DLNA URLs";
    }
}

void DlnaRemoteSourceProvider::persistUrls()
{
    QJsonArray array;
    {
        const std::scoped_lock lock{m_mutex};
        for(const QString& url : m_dlnaUrls) {
            array.append(url);
        }
    }

    const QString path = urlsFilePath();
    if(!QDir().mkpath(path.mid(0, path.lastIndexOf(u'/')))) {
        qCWarning(DLNA_PROVIDER) << "Cannot create path:" << path;
        return;
    }

    QFile file{path};
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(DLNA_PROVIDER) << "Cannot write DLNA URL list:" << path;
        return;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
} // namespace Fooyin::Dlna
