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

#include "providers/provider.hpp"
#include "providers/twitch/twitch-chat.hpp"
#include "providers/twitch/twitch-eventsub.hpp"

#include <QSet>
#include <QStringList>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace sf::twitch {

/* Supervises the Twitch chat and EventSub connections on one worker thread: applies login
 * changes, validates/refreshes tokens, reconnects with backoff and reports status. */
class TwitchProvider : public Provider {
public:
	TwitchProvider() = default;
	~TwitchProvider() override { stop(); }

	QString id() const override { return QStringLiteral("twitch"); }
	QString displayName() const override { return QStringLiteral("Twitch"); }
	bool supportsChat() const override { return true; }

	void start() override;
	void stop() override;
	void setChannels(const QStringList &channels) override;
	void reloadConfig() override;

private:
	struct Account {
		QString clientId;
		QString accessToken;
		QString refreshToken;
		QString login;
		QString userId;
		qint64 expiresAt = 0;
		bool eventsEnabled = true;

		bool loggedIn() const { return !clientId.isEmpty() && !accessToken.isEmpty() && !userId.isEmpty(); }
		Credentials creds() const { return {clientId, accessToken}; }
	};

	void run();
	void wake();
	bool applyConfig();
	bool refreshAccount(const char *reason);
	void superviseChat();
	void superviseEventSub();
	void updateStatus();

	std::thread worker;
	std::mutex mutex;
	std::condition_variable cv;
	bool stopping = false;
	bool configDirty = true;
	bool forceReconnect = false;
	QStringList requestedChannels;
	QStringList pendingRooms;

	/* Worker-thread state */
	Account account;
	QString authError;
	qint64 lastValidateMs = 0;
	TwitchChat chat;
	TwitchEventSub eventSub;
	bool chatStarted = false;
	bool eventSubStarted = false;
	qint64 chatRetryAt = 0;
	int chatBackoff = 1;
	qint64 eventSubRetryAt = 0;
	int eventSubBackoff = 1;
	QStringList activeChannels;
	ProviderStatus::State lastState = ProviderStatus::State::Disabled;
	QString lastMessage;
};

} // namespace sf::twitch
