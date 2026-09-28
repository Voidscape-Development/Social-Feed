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

#include <QJsonObject>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace sf::youtube {

/* Reads the logged-in channel's live chat through the YouTube Data API: waits for an active
 * broadcast, then either polls liveChatMessages.list at the configured interval (never faster
 * than YouTube asks) or, experimentally, holds a liveChatMessages.streamList connection open.
 * Stays inside the daily quota budget either way. */
class YouTubeProvider : public Provider {
public:
	YouTubeProvider() = default;
	~YouTubeProvider() override { stop(); }

	QString id() const override { return QStringLiteral("youtube"); }
	QString displayName() const override { return QStringLiteral("YouTube"); }
	bool supportsChat() const override { return true; }

	void start() override;
	void stop() override;
	void reloadConfig() override;

private:
	struct Account {
		QString clientId;
		QString clientSecret;
		QString accessToken;
		QString refreshToken;
		QString channelId;
		QString login;
		QString displayName;
		qint64 expiresAt = 0;
		int pollSeconds = 8;
		bool streaming = false; /* experimental streamList transport */
		int streamCost = 5;     /* assumed quota units per stream connection */

		bool loggedIn() const
		{
			return !accessToken.isEmpty() && !refreshToken.isEmpty() && !channelId.isEmpty();
		}
	};

	void run();
	void applyConfig();
	bool refreshAccount();
	void findBroadcast();
	void pollChat();
	void streamChat();
	QString configKey(const QJsonObject &section) const;
	void updateStatus();
	void resetChat();

	std::thread worker;
	std::mutex mutex;
	std::condition_variable cv;
	bool stopping = false;
	bool configDirty = true;
	QString activeKey; /* settings the running stream depends on; guarded by mutex */
	std::atomic<bool> abortStream{false};

	/* Worker-thread state */
	Account account;
	QString error;
	QString liveChatId;
	QString broadcastTitle;
	QString pageToken;
	bool primed = false;
	qint64 nextBroadcastCheck = 0;
	qint64 nextPoll = 0;
	QString pausedDay;

	/* streaming transport */
	bool streamFallback = false; /* streaming unusable this session; polling instead */
	QString streamFallbackReason;
	int streamFailures = 0; /* consecutive endpoint failures */
	int streamBackoff = 1;  /* seconds, after errors */
	int shortStreak = 0;    /* consecutive clean streams shorter than 5 s */
	std::deque<qint64> connectionTimes;
	qint64 streamMsThisHour = 0;
	ProviderStatus::State lastState = ProviderStatus::State::Disabled;
	QString lastMessage;
};

} // namespace sf::youtube
