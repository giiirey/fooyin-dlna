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

#include "dlnaplugin.h"
#include "dlnabrowserdialog.h"
#include "dlnaremotesourceprovider.h"

#include <core/engine/audioloader.h>
#include <core/network/networkaccessmanager.h>
#include <gui/guiconstants.h>
#include <gui/iconloader.h>
#include <utils/actions/actioncontainer.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/utils.h>

#include <QAction>
#include <QMainWindow>

using namespace Qt::StringLiterals;

namespace Fooyin {
void DlnaPlugin::initialise(const CorePluginContext& context)
{
    m_network = context.networkAccess;

    // Route known DLNA resource URLs through DlnaStreamDevice (stall
    // detection + byte-range resume); everything else falls through to
    // fooyin's stock RemoteIoService.
    m_provider = std::make_shared<Dlna::DlnaRemoteSourceProvider>(context.networkAccess, context.settingsManager);
    context.audioLoader->setRemoteSourceProvider(m_provider);
}

void DlnaPlugin::initialise(const GuiPluginContext& context)
{
    m_actionManager  = context.actionManager;
    m_trackSelection = context.trackSelection;

    auto* showAction = new QAction(tr("DLNA Browser"), this);
    showAction->setStatusTip(tr("Open the DLNA browser window"));
    Gui::setThemeIcon(showAction, Constants::Icons::AudioFile);

    auto* command = m_actionManager->registerAction(showAction, Id{u"Dlna.ShowDialog"_s});
    command->setCategories({tr("View")});
    m_actionManager->actionContainer(Constants::Menus::View)->addAction(command);

    connect(showAction, &QAction::triggered, this, &DlnaPlugin::showDlnaDialog);
}

void DlnaPlugin::showDlnaDialog()
{
    if(m_dialog) {
        m_dialog->show();
        m_dialog->raise();
        m_dialog->activateWindow();
        return;
    }

    m_dialog = new Dlna::DlnaBrowserDialog(m_network ? m_network.get() : nullptr, m_trackSelection,
                                           m_actionManager, m_provider ? m_provider.get() : nullptr,
                                           Utils::getMainWindow());
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_dialog->show();
}
} // namespace Fooyin
