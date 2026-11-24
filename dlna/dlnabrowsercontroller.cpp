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
    m_subtreeBrowse.reset();
    ++m_subtreeToken; // invalidate callbacks of a walk in flight
    m_subtreeItems.clear();
    m_subtreeQueue.clear();
    m_subtreeVisited = 0;
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

void DlnaBrowserController::browseSubtree(const QString& objectId, const DlnaEntryList& entries)
{
    if(!m_subtreeBrowse || objectId.isEmpty()) {
        return;
    }
    // Supersede any walk in flight: its callbacks check the token.
    ++m_subtreeToken;
    m_subtreeRoot = objectId;
    m_subtreeItems.clear();
    m_subtreeQueue.clear();
    m_subtreeVisited = 0;
    if(entries.empty()) {
        m_subtreeQueue.push_back({objectId, 0});
        fetchNextSubtreeContainer(m_subtreeToken);
    }
    else {
        processSubtreeLevel(m_subtreeToken, objectId, 0, entries);
    }
}

void DlnaBrowserController::processSubtreeLevel(int token, const QString& id, int depth,
                                                const DlnaEntryList& entries)
{
    if(token != m_subtreeToken) {
        return;
    }
    for(const DlnaEntry& entry : entries) {
        if(entry.isContainer) {
            if(depth + 1 > MaxSubtreeDepth) {
                // Items below this depth are not counted, so completeness cannot
                // be established: treat the folder as too large.
                finishSubtreeWalk(token, {}, true);
                return;
            }
            m_subtreeQueue.push_back({entry.id, depth + 1});
        }
        else {
            m_subtreeItems.push_back(entry);
            if(static_cast<int>(m_subtreeItems.size()) > MaxSubtreeItems) {
                finishSubtreeWalk(token, {}, true);
                return;
            }
        }
    }
    fetchNextSubtreeContainer(token);
}

void DlnaBrowserController::fetchNextSubtreeContainer(int token)
{
    if(token != m_subtreeToken) {
        return;
    }
    if(m_subtreeQueue.empty()) {
        finishSubtreeWalk(token, m_subtreeItems, false);
        return;
    }
    if(m_subtreeVisited >= MaxSubtreeContainers) {
        finishSubtreeWalk(token, {}, true);
        return;
    }
    const auto [id, depth] = m_subtreeQueue.front();
    m_subtreeQueue.erase(m_subtreeQueue.begin());
    ++m_subtreeVisited;
    m_subtreeBrowse->loadChildren(id, [this, token, id, depth](const DlnaEntryList& entries, bool ok,
                                                                const QString&) {
        if(token != m_subtreeToken) {
            return;
        }
        if(!ok) {
            // The walk was interrupted; completeness cannot be proven.
            finishSubtreeWalk(token, {}, true);
            return;
        }
        processSubtreeLevel(token, id, depth, entries);
    });
}

void DlnaBrowserController::finishSubtreeWalk(int token, const DlnaEntryList& entries, bool exceeded)
{
    if(token != m_subtreeToken) {
        return;
    }
    m_subtreeQueue.clear();

    DlnaEntryList result;
    result.reserve(entries.size());
    for(const DlnaEntry& entry : entries) {
        DlnaEntry copy = entry;
        copy.serverBase = m_info.base;
        result.push_back(std::move(copy));
    }
    // entries may alias m_subtreeItems, so copy first and clear afterwards.
    m_subtreeItems.clear();
    Q_EMIT subtreeLoaded(m_subtreeRoot, result, exceeded);
}

void DlnaBrowserController::reset()
{
    m_device.reset();
    m_browse.reset();
    m_subtreeBrowse.reset();
    ++m_subtreeToken; // invalidate callbacks of a walk in flight
    m_subtreeItems.clear();
    m_subtreeQueue.clear();
    m_subtreeVisited = 0;
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
    m_subtreeBrowse = std::make_unique<DlnaBrowse>(m_network, QUrl{info.controlUrl}, this);
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
