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

#include "dlnabrowserdialog.h"
#include "dlnatrack.h"

#include <core/track.h>
#include <gui/guiconstants.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>

#include <QAction>
#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace Fooyin::Dlna {
namespace {
QString formatDuration(qint64 ms)
{
    if(ms <= 0) {
        return {};
    }
    const int totalSec = static_cast<int>(ms / 1000);
    const int h = totalSec / 3600;
    const int m = (totalSec % 3600) / 60;
    const int s = totalSec % 60;
    if(h > 0) {
        return QString::asprintf("%d:%02d:%02d", h, m, s);
    }
    return QString::asprintf("%d:%02d", m, s);
}

QString formatSize(qint64 bytes)
{
    if(bytes <= 0) {
        return {};
    }
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while(v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    return QString::asprintf("%.1f %s", v, units[u]);
}

QString codecName(const QString& mimeType)
{
    const QString m = mimeType.toLower();
    if(m.contains(u"mpeg"_s) || m == u"audio/mpeg3"_s) return u"MP3"_s;
    if(m.contains(u"flac"_s)) return u"FLAC"_s;
    if(m.contains(u"ogg"_s) && m.contains(u"opus"_s)) return u"Opus"_s;
    if(m.contains(u"opus"_s)) return u"Opus"_s;
    if(m.contains(u"ogg"_s)) return u"Ogg"_s;
    if(m.contains(u"wav"_s)) return u"WAV"_s;
    if(m.contains(u"mp4"_s) || m.contains(u"aac"_s) || m.contains(u"m4a"_s)) return u"M4A"_s;
    return {};
}
} // namespace

// ---------------------------------------------------------------------------
// DlnaItemsModel
// ---------------------------------------------------------------------------
DlnaItemsModel::DlnaItemsModel(QObject* parent)
    : QAbstractTableModel{parent}
{ }

void DlnaItemsModel::setEntries(const DlnaEntryList& entries)
{
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

const DlnaEntryList& DlnaItemsModel::entries() const
{
    return m_entries;
}

DlnaEntry DlnaItemsModel::entryAt(int row) const
{
    if(row >= 0 && row < static_cast<int>(m_entries.size())) {
        return m_entries.at(row);
    }
    return {};
}

int DlnaItemsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int DlnaItemsModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DlnaItemsModel::data(const QModelIndex& index, int role) const
{
    if(!index.isValid() || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }
    const DlnaEntry& e = m_entries.at(index.row());

    if(role == Qt::DisplayRole) {
        switch(static_cast<Column>(index.column())) {
        case Title: return e.title;
        case Artist: return e.artist;
        case Album: return e.album;
        case Genre: return e.genre;
        case Duration: return formatDuration(e.durationMs);
        case Format: return codecName(e.mimeType);
        case Bitrate: return e.bitrate > 0 ? QString::number(e.bitrate) : QString{};
        case Size: return formatSize(e.size);
        default: return {};
        }
    }
    if(role == Qt::ToolTipRole && index.column() == Title) {
        return e.res;
    }
    return {};
}

QVariant DlnaItemsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch(static_cast<Column>(section)) {
    case Title: return QObject::tr("Title");
    case Artist: return QObject::tr("Artist");
    case Album: return QObject::tr("Album");
    case Genre: return QObject::tr("Genre");
    case Duration: return QObject::tr("Length");
    case Format: return QObject::tr("Format");
    case Bitrate: return QObject::tr("Bitrate");
    case Size: return QObject::tr("Size");
    default: return {};
    }
}

// ---------------------------------------------------------------------------
// DlnaBrowserDialog
// ---------------------------------------------------------------------------
DlnaBrowserDialog::DlnaBrowserDialog(QNetworkAccessManager* network, TrackSelectionController* trackSelection,
                                     ActionManager* actionManager, DlnaRemoteSourceProvider* provider,
                                     QWidget* parent)
    : QDialog{parent}
    , m_controller{new DlnaBrowserController(network, this)}
    , m_trackSelection{trackSelection}
    , m_actionManager{actionManager}
    , m_provider{provider}
    , m_addCurrentAction{nullptr}
    , m_context{nullptr}
    , m_serverEdit{nullptr}
    , m_connectButton{nullptr}
    , m_statusLabel{nullptr}
    , m_tree{nullptr}
    , m_table{nullptr}
    , m_itemsModel{nullptr}
    , m_playButton{nullptr}
    , m_addButton{nullptr}
    , m_queueButton{nullptr}
    , m_countLabel{nullptr}
{
    setupUi();

    IdList ids;
    ids.push_back(Id{Constants::Context::TrackSelection});
    ids.push_back(Id{u"Fooyin.Context.Dlna"_s});
    m_context = new WidgetContext(this, Context{ids}, this);

    if(m_actionManager) {
        if(auto* cmd = m_actionManager->command(Id{Constants::Actions::AddToCurrent})) {
            m_addCurrentAction = cmd->action();
            // The standard "Add to current playlist" action is kept enabled/disabled
            // by the track selection controller; follow it to keep our button in sync.
            connect(m_addCurrentAction, &QAction::changed, this, &DlnaBrowserDialog::updateActionState);
        }
    }

    connect(m_controller, &DlnaBrowserController::connecting, this,
            [this](const QString& hostPort) { setStatus(tr("Connecting to %1…").arg(hostPort)); });
    connect(m_controller, &DlnaBrowserController::connected, this, &DlnaBrowserDialog::onConnected);
    connect(m_controller, &DlnaBrowserController::connectFailed, this, &DlnaBrowserDialog::onConnectFailed);
    connect(m_controller, &DlnaBrowserController::childrenLoaded, this, &DlnaBrowserDialog::onChildrenLoaded);
}

void DlnaBrowserDialog::setupUi()
{
    setWindowTitle(tr("DLNA Browser"));
    resize(920, 560);

    auto* mainLayout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel(tr("Server:")));
    m_serverEdit = new QLineEdit;
    m_serverEdit->setPlaceholderText(tr("host:port, e.g. 192.168.1.1:8200"));
    m_serverEdit->setClearButtonEnabled(true);
    m_connectButton = new QPushButton(tr("Connect"));
    topRow->addWidget(m_serverEdit, 1);
    topRow->addWidget(m_connectButton);
    mainLayout->addLayout(topRow);

    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setItemsExpandable(true);
    m_tree->setExpandsOnDoubleClick(true);

    m_itemsModel = new DlnaItemsModel(this);
    m_table = new QTableView;
    m_table->setModel(m_itemsModel);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_tree);
    splitter->addWidget(m_table);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter, 1);

    auto* bottomRow = new QHBoxLayout;
    m_playButton = new QPushButton(tr("Play"));
    m_addButton = new QPushButton(tr("Add to playlist"));
    m_queueButton = new QPushButton(tr("Queue"));
    m_countLabel = new QLabel;
    m_playButton->setEnabled(false);
    m_addButton->setEnabled(false);
    m_queueButton->setEnabled(false);
    bottomRow->addWidget(m_playButton);
    bottomRow->addWidget(m_addButton);
    bottomRow->addWidget(m_queueButton);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_countLabel);
    mainLayout->addLayout(bottomRow);

    connect(m_connectButton, &QPushButton::clicked, this, &DlnaBrowserDialog::onConnect);
    connect(m_playButton, &QPushButton::clicked, this, &DlnaBrowserDialog::playSelected);
    connect(m_addButton, &QPushButton::clicked, this, &DlnaBrowserDialog::addToPlaylist);
    connect(m_queueButton, &QPushButton::clicked, this, &DlnaBrowserDialog::queueSelected);
    connect(m_table, &QTableView::doubleClicked, this, [this](const QModelIndex&) { playSelected(); });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        const QString objectId = item->data(0, Qt::UserRole).toString();
        if(!objectId.isEmpty()) {
            loadContainer(objectId);
        }
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if(!current) {
                    return;
                }
                const QString objectId = current->data(0, Qt::UserRole).toString();
                if(!objectId.isEmpty()) {
                    loadContainer(objectId);
                }
            });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&) { updateActionState(); });
}

void DlnaBrowserDialog::onConnect()
{
    const QString hostPort = m_serverEdit->text().trimmed();
    if(hostPort.isEmpty()) {
        setStatus(tr("Enter a server address."));
        return;
    }
    m_tree->clear();
    m_treeItems.clear();
    m_itemsModel->setEntries({});
    m_connected = false;
    m_pendingObjectId.clear();
    m_loadedObjects.clear();
    m_connectButton->setEnabled(false);
    m_controller->connectServer(hostPort);
}

void DlnaBrowserDialog::onConnected(const DlnaDeviceInfo& info)
{
    m_connected = true;
    m_connectButton->setEnabled(true);
    setStatus(tr("Connected to %1 (%2)").arg(info.friendlyName, info.base));
    // Go through loadContainer() so the root browse is tracked in
    // m_pendingObjectId and its response is not discarded as "stale".
    loadContainer(u"0"_s);
}

void DlnaBrowserDialog::onConnectFailed(const QString& error)
{
    m_connected = false;
    m_connectButton->setEnabled(true);
    setStatus(tr("Connection failed: %1").arg(error));
}

void DlnaBrowserDialog::onChildrenLoaded(const QString& objectId, const DlnaEntryList& entries, bool ok,
                                         const QString& error)
{
    if(objectId != m_pendingObjectId) {
        // Stale response from a superseded browse request.
        return;
    }
    m_pendingObjectId.clear();

    if(!ok) {
        setStatus(tr("Browse failed: %1").arg(error));
        return;
    }

    // Remember that this container has been browsed so that re-expanding
    // it (or a stale signal) does not add its children a second time.
    m_loadedObjects.insert(objectId);

    DlnaEntryList containers;
    DlnaEntryList items;
    for(const DlnaEntry& e : entries) {
        if(e.isContainer) {
            containers.push_back(e);
        }
        else {
            items.push_back(e);
        }
    }

    addContainerItems(objectId, containers);
    setTableEntries(items);
}

void DlnaBrowserDialog::addContainerItems(const QString& parentObjectId, const DlnaEntryList& containers)
{
    QTreeWidgetItem* parent = (parentObjectId == u"0"_s) ? nullptr : findTreeItem(parentObjectId);
    for(const DlnaEntry& c : containers) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        item->setText(0, c.title);
        item->setData(0, Qt::UserRole, c.id);
        m_treeItems.insert(c.id, item);
    }
    // Expand the first level for convenience.
    if(parentObjectId == u"0"_s) {
        m_tree->expandAll();
    }
}

QTreeWidgetItem* DlnaBrowserDialog::findTreeItem(const QString& objectId) const
{
    return m_treeItems.value(objectId, nullptr);
}

void DlnaBrowserDialog::loadContainer(const QString& objectId)
{
    if(!m_connected || objectId.isEmpty() || m_pendingObjectId == objectId
       || m_loadedObjects.contains(objectId)) {
        return;
    }
    m_pendingObjectId = objectId;
    m_controller->browseChildren(objectId);
}

void DlnaBrowserDialog::setTableEntries(const DlnaEntryList& items)
{
    m_itemsModel->setEntries(items);
    m_countLabel->setText(tr("%1 items").arg(static_cast<int>(items.size())));
    updateActionState();
}

void DlnaBrowserDialog::playSelected()
{
    if(!selectedEntries().empty()) {
        executeAction(TrackAction::Play);
    }
}

void DlnaBrowserDialog::addToPlaylist()
{
    if(!selectedEntries().empty()) {
        executeAction(TrackAction::AddCurrentPlaylist);
    }
}

void DlnaBrowserDialog::queueSelected()
{
    if(!selectedEntries().empty()) {
        executeAction(TrackAction::AddToQueue);
    }
}

void DlnaBrowserDialog::executeAction(TrackAction action)
{
    if(!m_trackSelection) {
        return;
    }
    const DlnaEntryList selected = selectedEntries();
    if(selected.empty()) {
        return;
    }

    TrackSelection selection;
    for(const DlnaEntry& e : selected) {
        selection.tracks.push_back(trackForEntry(e, m_provider));
    }
    m_trackSelection->changeSelectedTracks(m_context, selection);

    PlaylistAction::ActionOptions options{};
    if(action == TrackAction::Play) {
        options |= PlaylistAction::TempPlaylist;
        options |= PlaylistAction::StartPlayback;
    }
    m_trackSelection->executeAction(action, options);
}

bool DlnaBrowserDialog::addCurrentPlaylistEnabled() const
{
    if(!m_trackSelection || !m_addCurrentAction) {
        return true;
    }

    TrackSelection selection;
    for(const DlnaEntry& e : selectedEntries()) {
        selection.tracks.push_back(trackForEntry(e, m_provider));
    }

    // Unknown actions are treated as enabled so the button is not
    // inadvertently disabled if the standard action is missing.
    return m_trackSelection->selectionActionEnabled(m_addCurrentAction, selection).value_or(true);
}

void DlnaBrowserDialog::updateActionState()
{
    const int count = selectedEntries().size();
    const bool hasAny = !m_itemsModel->entries().empty();
    const bool hasSel = count > 0;
    m_playButton->setEnabled(m_connected && hasAny);
    m_addButton->setEnabled(m_connected && hasSel && addCurrentPlaylistEnabled());
    m_queueButton->setEnabled(m_connected && hasSel);
    if(hasSel) {
        m_countLabel->setText(tr("%1 of %2 selected").arg(count).arg(static_cast<int>(m_itemsModel->entries().size())));
    }
    else {
        m_countLabel->setText(tr("%1 items").arg(static_cast<int>(m_itemsModel->entries().size())));
    }
}

DlnaEntryList DlnaBrowserDialog::selectedEntries() const
{
    DlnaEntryList result;
    if(!m_table) {
        return result;
    }
    const auto* selection = m_table->selectionModel();
    if(!selection) {
        return result;
    }
    const QModelIndexList indexes = selection->selectedRows();
    for(const QModelIndex& index : indexes) {
        result.push_back(m_itemsModel->entryAt(index.row()));
    }
    return result;
}

void DlnaBrowserDialog::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
}
} // namespace Fooyin::Dlna
