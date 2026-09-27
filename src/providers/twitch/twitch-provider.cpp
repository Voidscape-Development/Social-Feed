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

#include "providers/twitch/twitch-provider.hpp"

#include "core/config-store.hpp"
#include "core/feed-types.hpp"
#include "providers/twitch/twitch-auth.hpp"

#include <plugin-support.h>
#include <obs-module.h>

#include <algorithm>
#include <chrono>

namespace sf::twitch {

namespace {
constexpr qint64 kValidateIntervalMs = 60LL * 60 * 1000; /* Twitch requires hourly validation */
constexpr qint64 kRefreshMarginMs = 10LL * 60 * 1000;
constexpr int kMaxBackoffSeconds = 60;
} // namespace

void TwitchProvider::start()
{
	if (worker.joinable())
		return;
	{
		std::lock_guard lock(mutex);
		stopping = false;
		configDirty = true;
	}
	worker = std::thread(&TwitchProvider::run, this);
}

void TwitchProvider::stop()
{
	{
		std::lock_guard lock(mutex);
		stopping = true;
	}
	cv.notify_all();
	if (worker.joinable())
		worker.join();
	chat.disconnect();
	eventSub.disconnect();
}

void TwitchProvider::wake()
{
	cv.notify_all();
}

void TwitchProvider::setChannels(const QStringList &channels)
{
	{
		std::lock_guard lock(mutex);
		requestedChannels = channels;
	}
	wake();
}

void TwitchProvider::reloadConfig()
{
	{
		std::lock_guard lock(mutex);
		configDirty = true;
	}
	wake();
}

void TwitchProvider::run()
{
	chat.setRoomCallback([this](const QString &, const QString &roomId) {
		{
			std::lock_guard lock(mutex);
			pendingRooms.append(roomId);
		}
		wake();
	});

	for (;;) {
		bool dirty;
		QStringList rooms;
		{
			std::unique_lock lock(mutex);
			cv.wait_for(lock, std::chrono::seconds(1));
			if (stopping)
				break;
			dirty = configDirty;
			configDirty = false;
			rooms.swap(pendingRooms);
		}

		if (dirty && applyConfig()) {
			/* Credentials changed: rebuild both connections. */
			chat.disconnect();
			eventSub.disconnect();
			chatStarted = eventSubStarted = false;
			chatRetryAt = eventSubRetryAt = 0;
			chatBackoff = eventSubBackoff = 1;
			TwitchCache::instance().clear();
		}

		if (account.loggedIn()) {
			qint64 now = nowMs();
			bool expiring = account.expiresAt > 0 && account.expiresAt - now < kRefreshMarginMs;
			if (expiring || eventSub.unauthorized())
				refreshAccount(expiring ? "token expiring" : "Helix returned 401");
			else if (now - lastValidateMs > kValidateIntervalMs) {
				ValidateResult v = validateToken(account.accessToken);
				lastValidateMs = now;
				if (v.unauthorized)
					refreshAccount("hourly validation failed");
			}
			for (const QString &room : rooms)
				TwitchCache::instance().ensureBadges(account.creds(), room);
		}

		superviseEventSub();
		superviseChat();
		updateStatus();
	}

	chat.disconnect();
	eventSub.disconnect();
	chatStarted = eventSubStarted = false;
}

/* Returns true when the credentials in use changed. */
bool TwitchProvider::applyConfig()
{
	QJsonObject section = ConfigStore::instance().section("twitch");
	Account next;
	next.clientId = clientId();
	next.accessToken = section.value("accessToken").toString();
	next.refreshToken = section.value("refreshToken").toString();
	next.login = section.value("login").toString().toLower();
	next.userId = section.value("userId").toString();
	next.expiresAt = (qint64)section.value("expiresAt").toDouble();
	next.eventsEnabled = section.value("eventsEnabled").toBool(true);

	bool changed = next.clientId != account.clientId || next.accessToken != account.accessToken ||
		       next.login != account.login || next.eventsEnabled != account.eventsEnabled;
	account = next;
	if (!changed)
		return false;

	authError.clear();
	if (!account.accessToken.isEmpty()) {
		ValidateResult v = validateToken(account.accessToken);
		lastValidateMs = nowMs();
		if (v.unauthorized) {
			refreshAccount("stored token invalid");
		} else if (v.ok && (v.login != account.login || v.userId != account.userId)) {
			/* Keep identity in sync with what the token actually belongs to. */
			account.login = v.login;
			account.userId = v.userId;
			ConfigStore::instance().updateSection("twitch", {{"login", v.login}, {"userId", v.userId}});
		}
	}
	return true;
}

bool TwitchProvider::refreshAccount(const char *reason)
{
	obs_log(LOG_INFO, "Refreshing Twitch token (%s)", reason);
	if (account.refreshToken.isEmpty() || account.clientId.isEmpty()) {
		authError = QStringLiteral("Twitch login expired, please log in again");
		account.accessToken.clear();
		return false;
	}

	TokenResult token = twitch::refreshToken(account.clientId, account.refreshToken);
	if (token.status != TokenResult::Status::Success) {
		obs_log(LOG_WARNING, "Twitch token refresh failed: %s", token.error.toUtf8().constData());
		authError = QStringLiteral("Twitch login expired, please log in again");
		account.accessToken.clear();
		return false;
	}

	ValidateResult identity = validateToken(token.accessToken);
	lastValidateMs = nowMs();
	account.accessToken = token.accessToken;
	account.refreshToken = token.refreshToken;
	account.expiresAt = nowMs() + token.expiresInSeconds * 1000;
	authError.clear();

	/* storeLogin triggers reloadConfig(); applyConfig() then sees the same token and leaves the
	 * connections alone, so reconnect explicitly with the new credentials here. */
	storeLogin(token, identity);
	chat.disconnect();
	eventSub.disconnect();
	chatStarted = eventSubStarted = false;
	chatRetryAt = eventSubRetryAt = 0;
	return true;
}

void TwitchProvider::superviseChat()
{
	QStringList channels;
	{
		std::lock_guard lock(mutex);
		channels = requestedChannels;
	}
	if (account.loggedIn() && !channels.contains(account.login))
		channels.append(account.login);
	activeChannels = channels;

	if (channels.isEmpty()) {
		if (chatStarted) {
			chat.disconnect();
			chatStarted = false;
		}
		return;
	}

	qint64 now = nowMs();
	if (chatStarted && chat.needsReconnect()) {
		chat.disconnect();
		chatStarted = false;
		chatRetryAt = now + chatBackoff * 1000LL;
		chatBackoff = std::min(chatBackoff * 2, kMaxBackoffSeconds);
		obs_log(LOG_INFO, "Twitch chat disconnected (%s), retrying", chat.lastError().toUtf8().constData());
	}

	if (!chatStarted && now >= chatRetryAt) {
		TwitchChat::Options options;
		if (account.loggedIn()) {
			options.creds = account.creds();
			options.login = account.login;
		}
		chat.connect(options);
		chatStarted = true;
	}

	if (chat.isConnected())
		chatBackoff = 1;

	QSet<QString> covered;
	if (eventSub.isConnected())
		covered.insert(account.login);
	chat.setEventSubChannels(covered);
	chat.setChannels(channels);
}

void TwitchProvider::superviseEventSub()
{
	bool wanted = account.loggedIn() && account.eventsEnabled;
	if (!wanted) {
		if (eventSubStarted) {
			eventSub.disconnect();
			eventSubStarted = false;
		}
		return;
	}

	qint64 now = nowMs();
	std::string url = "wss://eventsub.wss.twitch.tv/ws";
	if (eventSubStarted && eventSub.needsReconnect()) {
		std::string migrate = eventSub.takeReconnectUrl();
		eventSub.disconnect();
		eventSubStarted = false;
		if (!migrate.empty()) {
			url = migrate;
			eventSubRetryAt = 0;
		} else {
			eventSubRetryAt = now + eventSubBackoff * 1000LL;
			eventSubBackoff = std::min(eventSubBackoff * 2, kMaxBackoffSeconds);
			obs_log(LOG_INFO, "Twitch EventSub disconnected (%s), retrying",
				eventSub.lastError().toUtf8().constData());
		}
	}

	if (!eventSubStarted && now >= eventSubRetryAt) {
		eventSub.connect({account.creds(), account.userId, account.login}, url);
		eventSubStarted = true;
	}
	if (eventSub.isConnected())
		eventSubBackoff = 1;
}

void TwitchProvider::updateStatus()
{
	ProviderStatus::State state;
	QString message;

	if (clientId().isEmpty() && account.accessToken.isEmpty() && activeChannels.isEmpty()) {
		state = ProviderStatus::State::Disabled;
		message = QStringLiteral("Add a channel to a Chat Feed or log in");
	} else if (!authError.isEmpty()) {
		state = ProviderStatus::State::Error;
		message = authError;
	} else if (activeChannels.isEmpty() && !account.loggedIn()) {
		state = ProviderStatus::State::Disabled;
		message = QStringLiteral("Not logged in");
	} else {
		bool chatOk = activeChannels.isEmpty() || chat.isConnected();
		bool eventsWanted = account.loggedIn() && account.eventsEnabled;
		bool eventsOk = !eventsWanted || eventSub.isConnected();

		QStringList parts;
		if (!activeChannels.isEmpty())
			parts.append(chat.isConnected() ? QStringLiteral("Chat: %1").arg(activeChannels.join(", "))
							: QStringLiteral("Chat connecting"));
		if (eventsWanted) {
			QString subError = eventSub.lastError();
			if (eventSub.isConnected())
				parts.append(subError.isEmpty() ? QStringLiteral("Events: on")
								: QStringLiteral("Events: %1").arg(subError));
			else
				parts.append(QStringLiteral("Events connecting"));
		}
		state = chatOk && eventsOk ? ProviderStatus::State::Connected : ProviderStatus::State::Connecting;
		if (account.loggedIn())
			parts.prepend(QStringLiteral("Logged in as %1").arg(account.login));
		message = parts.join(QStringLiteral(" · "));
	}

	if (state != lastState || message != lastMessage) {
		lastState = state;
		lastMessage = message;
		reportStatus(state, message);
	}
}

} // namespace sf::twitch
