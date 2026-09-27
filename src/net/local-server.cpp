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

#include "net/local-server.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMimeDatabase>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace sf::net {

namespace {

constexpr qint64 kMaxRequestHeader = 16 * 1024;

QString overlayRoot()
{
	char *path = obs_module_file("overlay");
	QString root = path ? QFileInfo(QString::fromUtf8(path)).canonicalFilePath() : QString();
	bfree(path);
	return root;
}

QByteArray statusText(int status)
{
	switch (status) {
	case 200:
		return "OK";
	case 206:
		return "Partial Content";
	case 400:
		return "Bad Request";
	case 404:
		return "Not Found";
	case 405:
		return "Method Not Allowed";
	case 416:
		return "Range Not Satisfiable";
	default:
		return "Error";
	}
}

void writeResponse(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
		   const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {}, bool headOnly = false,
		   qint64 contentLength = -1)
{
	QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + statusText(status) + "\r\n";
	head += "Content-Type: " + contentType + "\r\n";
	head += "Content-Length: " + QByteArray::number(contentLength >= 0 ? contentLength : body.size()) + "\r\n";
	head += "Cache-Control: no-store\r\n";
	head += "Access-Control-Allow-Origin: *\r\n";
	head += "Connection: close\r\n";
	for (const auto &[key, value] : extraHeaders)
		head += key + ": " + value + "\r\n";
	head += "\r\n";

	socket->write(head);
	if (!headOnly)
		socket->write(body);
	socket->disconnectFromHost();
}

QByteArray mimeFor(const QString &path)
{
	QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == "html")
		return "text/html; charset=utf-8";
	if (suffix == "js")
		return "text/javascript; charset=utf-8";
	if (suffix == "css")
		return "text/css; charset=utf-8";
	if (suffix == "json")
		return "application/json";
	if (suffix == "svg")
		return "image/svg+xml";
	static QMimeDatabase db;
	return db.mimeTypeForFile(path).name().toUtf8();
}

} // namespace

LocalServer &LocalServer::instance()
{
	static LocalServer server;
	return server;
}

bool LocalServer::start()
{
	if (server)
		return true;

	server = new QTcpServer(this);
	if (!server->listen(QHostAddress::LocalHost, 0)) {
		obs_log(LOG_ERROR, "Local overlay server failed to listen: %s",
			server->errorString().toUtf8().constData());
		delete server;
		server = nullptr;
		return false;
	}
	listenPort = server->serverPort();
	connect(server, &QTcpServer::newConnection, this, [this]() {
		while (QTcpSocket *socket = server->nextPendingConnection())
			handleConnection(socket);
	});
	obs_log(LOG_INFO, "Overlay server listening on %s", baseUrl().toUtf8().constData());
	return true;
}

void LocalServer::stop()
{
	if (!server)
		return;
	server->close();
	delete server;
	server = nullptr;
	listenPort = 0;
}

QString LocalServer::baseUrl() const
{
	return QStringLiteral("http://127.0.0.1:%1").arg(listenPort);
}

QString LocalServer::overlayUrl(const QString &page, const QString &token) const
{
	return QStringLiteral("%1/overlay/%2?source=%3").arg(baseUrl(), page, token);
}

void LocalServer::registerSource(const QString &token, ConfigProvider provider)
{
	std::lock_guard lock(sourcesMutex);
	sources.insert(token, std::move(provider));
}

void LocalServer::unregisterSource(const QString &token)
{
	std::lock_guard lock(sourcesMutex);
	sources.remove(token);
}

QString LocalServer::registerMedia(const QString &localPath)
{
	if (localPath.isEmpty())
		return {};
	if (localPath.startsWith("http://") || localPath.startsWith("https://"))
		return localPath;

	QFileInfo info(localPath);
	if (!info.isFile())
		return {};

	QString canonical = info.canonicalFilePath();
	QString id = QString::fromLatin1(
		QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
	{
		std::lock_guard lock(mediaMutex);
		media.insert(id, canonical);
	}
	return QStringLiteral("%1/media/%2/%3")
		.arg(baseUrl(), id, QString::fromUtf8(QUrl::toPercentEncoding(info.fileName())));
}

void LocalServer::handleConnection(QTcpSocket *socket)
{
	connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	/* Drop idle or slow connections. */
	QPointer<QTcpSocket> guard(socket);
	QTimer::singleShot(15000, socket, [guard]() {
		if (guard)
			guard->abort();
	});

	connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
		if (socket->property("handled").toBool())
			return;
		QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
		qsizetype end = buffer.indexOf("\r\n\r\n");
		if (end < 0) {
			if (buffer.size() > kMaxRequestHeader)
				writeResponse(socket, 400, "text/plain", "Bad Request");
			else
				socket->setProperty("buffer", buffer);
			return;
		}
		socket->setProperty("handled", true);

		QList<QByteArray> lines = buffer.left(end).split('\n');
		QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
		if (requestLine.size() < 2) {
			writeResponse(socket, 400, "text/plain", "Bad Request");
			return;
		}
		QHash<QByteArray, QByteArray> headers;
		for (int i = 1; i < lines.size(); i++) {
			qsizetype colon = lines[i].indexOf(':');
			if (colon > 0)
				headers.insert(lines[i].left(colon).trimmed().toLower(),
					       lines[i].mid(colon + 1).trimmed());
		}
		respond(socket, requestLine[0], requestLine[1], headers);
	});
}

void LocalServer::respond(QTcpSocket *socket, const QByteArray &method, const QByteArray &target,
			  const QHash<QByteArray, QByteArray> &headers)
{
	bool headOnly = method == "HEAD";
	if (method != "GET" && !headOnly) {
		writeResponse(socket, 405, "text/plain", "Method Not Allowed");
		return;
	}

	QUrl url(QString::fromUtf8(target));
	QString path = url.path(QUrl::FullyDecoded);
	QStringList parts = path.split('/', Qt::SkipEmptyParts);

	if (parts.size() == 4 && parts[0] == "api" && parts[1] == "source" && parts[3] == "config") {
		QByteArray body;
		bool found = false;
		{
			std::lock_guard lock(sourcesMutex);
			auto it = sources.find(parts[2]);
			if (it != sources.end()) {
				body = it.value()();
				found = true;
			}
		}
		if (!found)
			writeResponse(socket, 404, "application/json", "{\"error\":\"unknown source\"}");
		else
			writeResponse(socket, 200, "application/json", body, {}, headOnly);
		return;
	}

	QString filePath;
	if (parts.size() >= 2 && parts[0] == "overlay") {
		static const QString root = overlayRoot();
		QString candidate = QFileInfo(root + "/" + parts.mid(1).join('/')).canonicalFilePath();
		if (!root.isEmpty() && candidate.startsWith(root + "/"))
			filePath = candidate;
	} else if (parts.size() >= 2 && parts[0] == "media") {
		std::lock_guard lock(mediaMutex);
		filePath = media.value(parts[1]);
	}

	QFile file(filePath);
	if (filePath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
		writeResponse(socket, 404, "text/plain", "Not Found");
		return;
	}

	qint64 size = file.size();
	QByteArray range = headers.value("range");
	if (range.startsWith("bytes=")) {
		QList<QByteArray> bounds = range.mid(6).split('-');
		qint64 start = bounds.value(0).isEmpty() ? -1 : bounds.value(0).toLongLong();
		qint64 last = bounds.value(1).isEmpty() ? size - 1 : bounds.value(1).toLongLong();
		if (start < 0) {
			/* suffix range: last N bytes */
			start = std::max<qint64>(0, size - last);
			last = size - 1;
		}
		last = std::min(last, size - 1);
		if (start > last || start >= size) {
			writeResponse(socket, 416, "text/plain", {},
				      {{"Content-Range", "bytes */" + QByteArray::number(size)}});
			return;
		}
		file.seek(start);
		QByteArray body = headOnly ? QByteArray() : file.read(last - start + 1);
		writeResponse(socket, 206, mimeFor(filePath), body,
			      {{"Accept-Ranges", "bytes"},
			       {"Content-Range", "bytes " + QByteArray::number(start) + "-" + QByteArray::number(last) +
							 "/" + QByteArray::number(size)}},
			      headOnly, last - start + 1);
		return;
	}

	QByteArray body = headOnly ? QByteArray() : file.readAll();
	writeResponse(socket, 200, mimeFor(filePath), body, {{"Accept-Ranges", "bytes"}}, headOnly, size);
}

} // namespace sf::net
