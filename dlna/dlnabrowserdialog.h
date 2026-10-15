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
#include "dlnabrowsercontroller.h"
#include "dlnaentry.h"

#include <gui/trackselectioncontroller.h>
#include <utils/actions/widgetcontext.h>

#include <QAbstractTableModel>
#include <QDialog>
#include <QHash>
#include <QSet>

class QAction;
class QLineEdit;
class QPushButton;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QTableView;

namespace Fooyin {
class ActionManager;
} // namespace Fooyin

namespace Fooyin::Dlna {
/*!
 * Table model over a list of DLNA items (playable media).
 */
class DlnaItemsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        Title = 0,
        Artist,
        Album,
        Genre,
        Duration,
        Format,
        Bitrate,
        Size,
        ColumnCount
    };

    explicit DlnaItemsModel(QObject* parent = nullptr);

    void setEntries(const DlnaEntryList& entries);
    [[nodiscard]] const DlnaEntryList& entries() const;
    [[nodiscard]] DlnaEntry entryAt(int row) const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex{}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                     int role = Qt::DisplayRole) const override;

private:
    DlnaEntryList m_entries;
};

/*!
 * Dialog that lets the user connect to a DLNA media server, browse its
 * container hierarchy, and send playable items to fooyin (play, add to
 * playlist, or queue).
 */
class DlnaRemoteSourceProvider;

class DlnaBrowserDialog : public QDialog
{
    Q_OBJECT

public:
    DlnaBrowserDialog(QNetworkAccessManager* network, TrackSelectionController* trackSelection,
                      ActionManager* actionManager, DlnaRemoteSourceProvider* provider,
                      QWidget* parent = nullptr);

private:
    void setupUi();
    void onConnect();
    void onConnected(const DlnaDeviceInfo& info);
    void onConnectFailed(const QString& error);
    void onChildrenLoaded(const QString& objectId, const DlnaEntryList& entries, bool ok, const QString& error);
    void addContainerItems(const QString& parentObjectId, const DlnaEntryList& containers);
    QTreeWidgetItem* findTreeItem(const QString& objectId) const;
    void loadContainer(const QString& objectId);
    void setTableEntries(const DlnaEntryList& items);
    void playSelected();
    void addToPlaylist();
    void queueSelected();
    void executeAction(TrackAction action);
    void updateActionState();
    [[nodiscard]] bool addCurrentPlaylistEnabled() const;
    [[nodiscard]] DlnaEntryList selectedEntries() const;
    void setStatus(const QString& text);

    DlnaBrowserController* m_controller;
    TrackSelectionController* m_trackSelection;
    ActionManager* m_actionManager;
    DlnaRemoteSourceProvider* m_provider;
    QAction* m_addCurrentAction;
    WidgetContext* m_context;

    QLineEdit* m_serverEdit;
    QPushButton* m_connectButton;
    QLabel* m_statusLabel;
    QTreeWidget* m_tree;
    QTableView* m_table;
    DlnaItemsModel* m_itemsModel;
    QPushButton* m_playButton;
    QPushButton* m_addButton;
    QPushButton* m_queueButton;
    QLabel* m_countLabel;

    QHash<QString, QTreeWidgetItem*> m_treeItems;
    QSet<QString> m_loadedObjects;
    QString m_pendingObjectId;
    bool m_connected{false};
};
} // namespace Fooyin::Dlna
