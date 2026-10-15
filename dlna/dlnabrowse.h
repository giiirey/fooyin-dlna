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

#include "dlnaentry.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>

#include <functional>

namespace Fooyin::Dlna {
/*!
 * Thin async wrapper around the DLNA ContentDirectory Browse action.
 *
 * Handles SOAP request construction, response parsing (DIDL-Lite), and
 * transparent pagination so callers receive the full child list of a
 * container in one callback.
 */
class DlnaBrowse : public QObject
{
    Q_OBJECT

public:
    using ChildrenCallback = std::function<void(const DlnaEntryList&, bool ok, const QString& error)>;

    DlnaBrowse(QNetworkAccessManager* network, const QUrl& controlUrl, QObject* parent);

    //! Loads all direct children of @p objectId (paginating internally).
    void loadChildren(const QString& objectId, ChildrenCallback callback);

    //! Parses a DIDL-Lite document (already extracted from the SOAP response).
    static DlnaEntryList parseDidl(const QByteArray& didl);
    //! Parses an UPnP duration string like "1:02:03" or "2:20.982" into milliseconds.
    static qint64 parseDuration(const QString& duration);

private:
    void fetchPage(const QString& objectId, int startIndex, int count, DlnaEntryList& acc);
    void handlePage(QNetworkReply* reply, const QString& objectId, int count, DlnaEntryList& acc);

    QNetworkAccessManager* m_network;
    QUrl m_controlUrl;
    QNetworkReply* m_pending{nullptr};
    QString m_objectId;
    DlnaEntryList m_acc;
    ChildrenCallback m_callback;
    static constexpr int PageSize = 500;
};
} // namespace Fooyin::Dlna
