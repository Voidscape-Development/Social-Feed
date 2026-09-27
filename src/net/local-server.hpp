/*
Social Feed
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>
#include <mutex>

class QTcpServer;
class QTcpSocket;

namespace sf::net {

/* Loopback-only HTTP server the overlay pages are loaded from.
 *
 *   GET /overlay/<file>              static files from the module's data/overlay directory
 *   GET /api/source/<token>/config   JSON config for one overlay source instance
 *   GET /media/<id>                  a local media file the user picked in the editor
 *
 * Media is only served for paths that were explicitly registered, so the server never exposes
 * arbitrary files. Lives on the Qt UI thread; the registries are thread-safe. */
class LocalServer : public QObject {
	Q_OBJECT

public:
	using ConfigProvider = std::function<QByteArray()>;

	static LocalServer &instance();

	bool start();
	void stop();

	quint16 port() const { return listenPort; }
	QString baseUrl() const;
	QString overlayUrl(const QString &page, const QString &token) const;

	void registerSource(const QString &token, ConfigProvider provider);
	void unregisterSource(const QString &token);

	/* Returns an URL the overlay can load the file from, or an empty string. */
	QString registerMedia(const QString &localPath);

private:
	LocalServer() = default;
	void handleConnection(QTcpSocket *socket);
	void respond(QTcpSocket *socket, const QByteArray &method, const QByteArray &target,
		     const QHash<QByteArray, QByteArray> &headers);

	QTcpServer *server = nullptr;
	quint16 listenPort = 0;

	/* Config providers run with sourcesMutex held (so a source cannot be destroyed mid-call)
	 * and may register media, hence the separate mediaMutex. */
	std::mutex sourcesMutex;
	QHash<QString, ConfigProvider> sources;
	std::mutex mediaMutex;
	QHash<QString, QString> media; /* id -> path */
};

} // namespace sf::net
