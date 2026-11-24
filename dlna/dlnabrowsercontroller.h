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

#include "dlnabrowse.h"
#include "dlnadevice.h"
#include "dlnaentry.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <memory>
#include <utility>
#include <vector>

namespace Fooyin::Dlna {
/*!
 * Orchestrates connecting to a DLNA server and browsing its content.
 *
 * Wraps DlnaDevice (server resolution) and DlnaBrowse (ContentDirectory
 * Browse) behind a small signal-based API so UI code stays decoupled from
 * the low-level UPnP plumbing.
 */
class DlnaBrowserController : public QObject
{
    Q_OBJECT

public:
    explicit DlnaBrowserController(QNetworkAccessManager* network, QObject* parent = nullptr);

    //! True while a server description is being fetched.
    bool isConnecting() const;
    //! True if a server has been successfully resolved.
    bool isConnected() const;
    [[nodiscard]] const DlnaDeviceInfo& deviceInfo() const;
    [[nodiscard]] QString serverBase() const;

    //! Resolves @p hostPort and, on success, prepares a browse endpoint.
    void connectServer(const QString& hostPort);
    //! Loads the direct children of @p objectId (root is "0").
    void browseChildren(const QString& objectId);
    /*!
     * Recursively lists playable items under @p objectId (files in subfolders
     * included), within the limits below. @p entries optionally seeds the walk
     * with the level already fetched for @p objectId, so that level is not
     * fetched twice.
     *
     * The walk is bounded: if the subtree does not fit within the limits (or a
     * browse fails mid-walk), completeness cannot be proven and subtreeLoaded
     * reports @p exceeded with an empty list, so callers show nothing.
     */
    void browseSubtree(const QString& objectId, const DlnaEntryList& entries = {});
    //! Aborts any in-flight operation.
    void reset();

Q_SIGNALS:
    void connecting(const QString& hostPort);
    void connected(const DlnaDeviceInfo& info);
    void connectFailed(const QString& error);
    void childrenLoading(const QString& objectId);
    void childrenLoaded(const QString& objectId, const DlnaEntryList& entries, bool ok, const QString& error);
    void subtreeLoaded(const QString& objectId, const DlnaEntryList& entries, bool exceeded);

private:
    void handleResolved(const DlnaDeviceInfo& info);
    void handleChildren(const QString& objectId, const DlnaEntryList& entries, bool ok, const QString& error);
    void processSubtreeLevel(int token, const QString& id, int depth, const DlnaEntryList& entries);
    void fetchNextSubtreeContainer(int token);
    void finishSubtreeWalk(int token, const DlnaEntryList& entries, bool exceeded);

    QNetworkAccessManager* m_network;
    DlnaDevice m_device;
    std::unique_ptr<DlnaBrowse> m_browse;
    std::unique_ptr<DlnaBrowse> m_subtreeBrowse; // separate instance: the tree may browse concurrently
    DlnaDeviceInfo m_info;
    bool m_connecting{false};

    int m_subtreeToken{0};
    QString m_subtreeRoot;
    DlnaEntryList m_subtreeItems;
    std::vector<std::pair<QString, int>> m_subtreeQueue; // pending containers: object id, depth
    int m_subtreeVisited{0};

    // Chosen to bound memory and network per folder selection.
    static constexpr int MaxSubtreeItems = 2000;     // ~1-2 MB worst case (walk + cache + model copies)
    static constexpr int MaxSubtreeContainers = 150; // <= ~150 SOAP requests plus pages per selection
    static constexpr int MaxSubtreeDepth = 10;
};
} // namespace Fooyin::Dlna

Q_DECLARE_METATYPE(Fooyin::Dlna::DlnaDeviceInfo)
Q_DECLARE_METATYPE(Fooyin::Dlna::DlnaEntry)
Q_DECLARE_METATYPE(Fooyin::Dlna::DlnaEntryList)
