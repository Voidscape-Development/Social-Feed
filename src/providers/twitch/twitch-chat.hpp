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

#include "net/websocket-client.hpp"
#include "providers/twitch/twitch-api.hpp"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <atomic>
#include <functional>
#include <mutex>

namespace sf::twitch {

struct IrcMessage {
	QHash<QString, QString> tags;
	QString prefix;
	QString command;
	QStringList params;

	QString tag(const QString &key) const { return tags.value(key); }
	QString nick() const { return prefix.section('!', 0, 0); }
	QString trailing() const { return params.isEmpty() ? QString() : params.last(); }
};

IrcMessage parseIrcLine(const QString &line);

/* Twitch chat over IRC-on-WebSocket (wss://irc-ws.chat.twitch.tv). Works anonymously for any
 * public channel; when logged in it authenticates so badges and avatars can be resolved.
 * Besides chat it turns USERNOTICE (subs, gifts, raids) and bits into events for channels that
 * are not covered by EventSub. */
class TwitchChat {
public:
	struct Options {
		Credentials creds;
		QString login; /* empty = anonymous */
		std::string url = "wss://irc-ws.chat.twitch.tv:443";
	};

	using RoomCallback = std::function<void(const QString &channel, const QString &roomId)>;

	TwitchChat() = default;
	~TwitchChat() { disconnect(); }

	void connect(const Options &options);
	void disconnect();

	bool isConnected() const { return socket.connected(); }
	/* True after the connection dropped and a reconnect is needed. */
	bool needsReconnect() const { return dropped; }
	QString lastError() const;

	void setChannels(const QStringList &channels);
	void setEventSubChannels(const QSet<QString> &channels);
	void setRoomCallback(RoomCallback callback) { roomCallback = std::move(callback); }

private:
	void handleLine(const QString &line);
	void handlePrivmsg(const IrcMessage &msg);
	void handleUserNotice(const IrcMessage &msg);
	QJsonObject buildUser(const IrcMessage &msg, const QString &channelRoomId) const;
	void sendRaw(const QString &line);
	void syncJoins();

	net::WebSocketClient socket;
	Options options;
	RoomCallback roomCallback;

	std::atomic<bool> dropped{false};
	std::atomic<bool> registered{false};

	mutable std::mutex mutex;
	QString error;
	QStringList wanted;
	QSet<QString> joined;
	QSet<QString> eventSubChannels;
	QHash<QString, QString> roomIds; /* channel -> room id */
};

} // namespace sf::twitch
