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

#include <core/plugins/coreplugin.h>
#include <core/plugins/coreplugincontext.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>
#include <gui/plugins/guiplugincontext.h>

#include <QPointer>
#include <QObject>

#include <memory>

namespace Fooyin {
class ActionManager;
class NetworkAccessManager;
class TrackSelectionController;

namespace Dlna {
class DlnaBrowserDialog;
class DlnaRemoteSourceProvider;
}

/*!
 * A fooyin plugin that adds a DLNA/UPnP media server browser.
 *
 * A View menu item opens the browser dialog, where the user can connect
 * to a server, navigate its content directory, and send playable items
 * to playback, the current playlist, or the queue.
 */
class DlnaPlugin : public QObject, public Plugin, public CorePlugin, public GuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID FOOYIN_PLUGIN_IID FILE "dlna.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

public:
    void initialise(const CorePluginContext& context) override;
    void initialise(const GuiPluginContext& context) override;

private:
    void showDlnaDialog();

    std::shared_ptr<NetworkAccessManager> m_network;
    std::shared_ptr<Dlna::DlnaRemoteSourceProvider> m_provider;
    ActionManager* m_actionManager{nullptr};
    TrackSelectionController* m_trackSelection{nullptr};
    QPointer<Dlna::DlnaBrowserDialog> m_dialog;
};
} // namespace Fooyin
