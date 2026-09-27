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

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <mutex>

namespace sf::twitch {

/* EventSub over WebSocket for the logged-in broadcaster's own channel: follows, subs, gifted
 * subs, resub messages, cheers, raids, channel point redemptions and hype trains. */
class TwitchEventSub {
public:
	struct Options {
		Credentials creds;
		QString userId;
		QString login;
	};

	TwitchEventSub() = default;
	~TwitchEventSub() { disconnect(); }

	void connect(const Options &options, const std::string &url = "wss://eventsub.wss.twitch.tv/ws");
	void disconnect();

	bool isConnected() const { return socket.connected() && welcomed; }
	bool needsReconnect() const;
	/* Twitch asked us to move to a new URL; subscriptions carry over. */
	std::string takeReconnectUrl();
	/* A 401 from Helix while subscribing; the provider should refresh the token. */
	bool unauthorized() const { return authFailed; }
	QString lastError() const;

private:
	void handleMessage(const std::string &data);
	void subscribeAll(const QString &sessionId);
	void handleNotification(const QString &type, const QJsonObject &event);

	net::WebSocketClient socket;
	Options options;

	std::atomic<bool> welcomed{false};
	std::atomic<bool> dropped{false};
	std::atomic<bool> authFailed{false};
	std::atomic<qint64> lastMessageMs{0};
	std::atomic<int> keepaliveSeconds{10};

	mutable std::mutex mutex;
	QString error;
	std::string reconnectUrl;
};

} // namespace sf::twitch
