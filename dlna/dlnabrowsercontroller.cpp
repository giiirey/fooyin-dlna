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

#include "dlnabrowsercontroller.h"

using namespace Qt::StringLiterals;

namespace Fooyin::Dlna {
DlnaBrowserController::DlnaBrowserController(QNetworkAccessManager* network, QObject* parent)
    : QObject{parent}
    , m_network{network}
    , m_device{network, this}
{
    qRegisterMetaType<DlnaDeviceInfo>();
    qRegisterMetaType<DlnaEntryList>();

    connect(&m_device, &DlnaDevice::resolved, this, &DlnaBrowserController::handleResolved);
}

bool DlnaBrowserController::isConnecting() const
{
    return m_connecting;
}

bool DlnaBrowserController::isConnected() const
{
    return m_info.valid && m_browse != nullptr;
}

const DlnaDeviceInfo& DlnaBrowserController::deviceInfo() const
{
    return m_info;
}

QString DlnaBrowserController::serverBase() const
{
    return m_info.base;
}

void DlnaBrowserController::connectServer(const QString& hostPort)
{
    m_connecting = true;
    m_browse.reset();
    m_info = {};
    Q_EMIT connecting(hostPort);
    m_device.resolve(hostPort);
}

void DlnaBrowserController::browseChildren(const QString& objectId)
{
    if(!m_browse) {
        return;
    }
    Q_EMIT childrenLoading(objectId);
    m_browse->loadChildren(
        objectId,
        [this, objectId](const DlnaEntryList& entries, bool ok, const QString& error) {
            handleChildren(objectId, entries, ok, error);
        });
}

void DlnaBrowserController::reset()
{
    m_device.reset();
    m_browse.reset();
    m_info = {};
    m_connecting = false;
}

void DlnaBrowserController::handleResolved(const DlnaDeviceInfo& info)
{
    m_connecting = false;
    if(!info.valid) {
        Q_EMIT connectFailed(info.error);
        return;
    }

    m_info = info;
    m_browse = std::make_unique<DlnaBrowse>(m_network, QUrl{info.controlUrl}, this);
    Q_EMIT connected(info);
}

void DlnaBrowserController::handleChildren(const QString& objectId, const DlnaEntryList& entries, bool ok,
                                           const QString& error)
{
    DlnaEntryList result = entries;
    for(auto& entry : result) {
        entry.serverBase = m_info.base;
    }
    Q_EMIT childrenLoaded(objectId, result, ok, error);
}
} // namespace Fooyin::Dlna
