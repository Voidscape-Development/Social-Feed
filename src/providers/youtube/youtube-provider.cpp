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

#include "providers/youtube/youtube-provider.hpp"

#include "core/config-store.hpp"
#include "core/event-bus.hpp"
#include "core/feed-types.hpp"
#include "net/http-client.hpp"
#include "providers/youtube/youtube-api.hpp"
#include "providers/youtube/youtube-chat-parser.hpp"
#include "providers/youtube/youtube-chat-stream.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonArray>

#include <algorithm>
#include <chrono>

namespace sf::youtube {

namespace {
constexpr qint64 kRefreshMarginMs = 5LL * 60 * 1000;
constexpr qint64 kBroadcastCheckMs = 60LL * 1000;
constexpr qint64 kErrorRetryMs = 30LL * 1000;
} // namespace

void YouTubeProvider::start()
{
	if (worker.joinable())
		return;
	{
		std::lock_guard lock(mutex);
		stopping = false;
		configDirty = true;
	}
	worker = std::thread(&YouTubeProvider::run, this);
}

void YouTubeProvider::stop()
{
	{
		std::lock_guard lock(mutex);
		stopping = true;
	}
	abortStream = true;
	cv.notify_all();
	if (worker.joinable())
		worker.join();
}

QString YouTubeProvider::configKey(const QJsonObject &section) const
{
	/* Changing any of these must end a running stream. A refreshed access token must not. */
	return QStringList{section.value("chatTransport").toString(), section.value("channelId").toString(),
			   section.value("clientId").toString(), section.value("apiBaseUrl").toString()}
		.join('|');
}

void YouTubeProvider::reloadConfig()
{
	QString key = configKey(ConfigStore::instance().section("youtube"));
	{
		std::lock_guard lock(mutex);
		configDirty = true;
		if (key != activeKey)
			abortStream = true;
	}
	cv.notify_all();
}

void YouTubeProvider::run()
{
	for (;;) {
		bool dirty;
		{
			std::unique_lock lock(mutex);
			cv.wait_for(lock, std::chrono::milliseconds(500));
			if (stopping)
				break;
			dirty = configDirty;
			configDirty = false;
		}

		if (dirty)
			applyConfig();

		if (account.loggedIn()) {
			if (!pausedDay.isEmpty() && Quota::currentDay() != pausedDay) {
				pausedDay.clear();
				error.clear();
			}

			qint64 now = nowMs();
			bool ready = pausedDay.isEmpty();
			if (ready && account.expiresAt - now < kRefreshMarginMs)
				ready = refreshAccount();

			if (ready && account.loggedIn()) {
				if (liveChatId.isEmpty()) {
					if (now >= nextBroadcastCheck)
						findBroadcast();
				} else if (now >= nextPoll) {
					if (account.streaming && !streamFallback)
						streamChat();
					else
						pollChat();
				}
			}
			Quota::instance().persist();
		}
		updateStatus();
	}
	Quota::instance().persist(true);
}

void YouTubeProvider::resetChat()
{
	liveChatId.clear();
	broadcastTitle.clear();
	pageToken.clear();
	primed = false;
	nextBroadcastCheck = 0;
	nextPoll = 0;
}

void YouTubeProvider::applyConfig()
{
	QJsonObject section = ConfigStore::instance().section("youtube");
	Account next;
	next.clientId = section.value("clientId").toString().trimmed();
	next.clientSecret = section.value("clientSecret").toString().trimmed();
	next.accessToken = section.value("accessToken").toString();
	next.refreshToken = section.value("refreshToken").toString();
	next.channelId = section.value("channelId").toString();
	next.login = section.value("login").toString();
	next.displayName = section.value("displayName").toString();
	next.expiresAt = (qint64)section.value("expiresAt").toDouble();
	next.pollSeconds = std::clamp(section.value("pollSeconds").toInt(kDefaultPollSeconds), 3, 120);
	next.streaming = section.value("chatTransport").toString() == "stream";
	next.streamCost = std::clamp(section.value("streamCost").toInt(5), 0, 100);
	{
		std::lock_guard lock(mutex);
		activeKey = configKey(section);
	}
	if (next.streaming != account.streaming) {
		streamFallback = false;
		streamFallbackReason.clear();
		streamFailures = 0;
		streamBackoff = 1;
	}

	bool identityChanged = next.accessToken != account.accessToken || next.channelId != account.channelId ||
			       next.clientId != account.clientId;
	account = next;
	if (identityChanged) {
		error.clear();
		pausedDay.clear();
		resetChat();
	}
}

bool YouTubeProvider::refreshAccount()
{
	oauth::TokenResult token = refreshToken({account.clientId, account.clientSecret}, account.refreshToken);
	if (token.status != oauth::TokenResult::Status::Success) {
		obs_log(LOG_WARNING, "YouTube token refresh failed: %s", token.error.toUtf8().constData());
		if (token.status == oauth::TokenResult::Status::Expired ||
		    token.status == oauth::TokenResult::Status::Denied) {
			error = QStringLiteral("YouTube login expired, please log in again");
			account.accessToken.clear();
		} else {
			error = QStringLiteral("Could not refresh the YouTube login: %1").arg(token.error);
			/* Retry on the next tick after a pause, not in a tight loop. */
			account.expiresAt = nowMs() + kErrorRetryMs + kRefreshMarginMs;
		}
		return false;
	}

	account.accessToken = token.accessToken;
	account.refreshToken = token.refreshToken;
	account.expiresAt = nowMs() + token.expiresInSeconds * 1000;
	error.clear();
	/* applyConfig() will see the token it already holds and keep the chat state. */
	ConfigStore::instance().updateSection("youtube", {{"accessToken", account.accessToken},
							  {"refreshToken", account.refreshToken},
							  {"expiresAt", account.expiresAt}});
	return true;
}

void YouTubeProvider::findBroadcast()
{
	qint64 now = nowMs();
	ApiResult result = get(account.accessToken,
			       "liveBroadcasts?part=snippet&broadcastStatus=active&broadcastType=all&maxResults=5",
			       kCostList);

	if (result.unauthorized()) {
		account.expiresAt = 0; /* refresh on the next tick */
		return;
	}
	if (result.quotaExceeded()) {
		pausedDay = Quota::currentDay();
		return;
	}
	if (!result.ok()) {
		error = result.describe();
		nextBroadcastCheck = now + kBroadcastCheckMs;
		return;
	}

	error.clear();
	for (const QJsonValue value : result.json.value("items").toArray()) {
		QJsonObject snippet = value.toObject().value("snippet").toObject();
		QString chatId = snippet.value("liveChatId").toString();
		if (chatId.isEmpty())
			continue;
		resetChat();
		liveChatId = chatId;
		broadcastTitle = snippet.value("title").toString();
		obs_log(LOG_INFO, "YouTube live chat found for \"%s\"", broadcastTitle.toUtf8().constData());
		return;
	}
	nextBroadcastCheck = now + kBroadcastCheckMs;
}

void YouTubeProvider::pollChat()
{
	qint64 now = nowMs();
	QString query = QStringLiteral("liveChat/messages?liveChatId=%1&part=snippet,authorDetails&maxResults=2000")
				.arg(QString::fromStdString(net::urlEncode(liveChatId.toStdString())));
	if (!pageToken.isEmpty())
		query += "&pageToken=" + QString::fromStdString(net::urlEncode(pageToken.toStdString()));

	ApiResult result = get(account.accessToken, query, kCostChatMessages);

	if (result.unauthorized()) {
		account.expiresAt = 0;
		return;
	}
	if (result.quotaExceeded()) {
		pausedDay = Quota::currentDay();
		return;
	}
	if (result.reason == "liveChatEnded" || result.reason == "liveChatNotFound" ||
	    result.reason == "liveChatDisabled" || result.http.status == 404) {
		obs_log(LOG_INFO, "YouTube live chat ended (%s)", result.reason.toUtf8().constData());
		resetChat();
		nextBroadcastCheck = now + kErrorRetryMs;
		return;
	}
	if (!result.ok()) {
		error = result.describe();
		nextPoll = now + kErrorRetryMs;
		return;
	}

	error.clear();
	pageToken = result.json.value("nextPageToken").toString();
	qint64 interval =
		std::max<qint64>(result.json.value("pollingIntervalMillis").toInteger(), account.pollSeconds * 1000LL);
	nextPoll = now + interval;

	ParseResult parsed = parseChatMessages(result.json.value("items").toArray(), account.login);
	/* The first page is the backlog from before we connected; don't replay it. */
	if (primed) {
		for (const FeedItem &item : parsed.items)
			EventBus::instance().publish(item);
	}
	primed = true;

	if (parsed.chatEnded || result.json.contains("offlineAt")) {
		resetChat();
		nextBroadcastCheck = now + kErrorRetryMs;
	}
}

void YouTubeProvider::streamChat()
{
	StreamConnection c;
	c.accessToken = account.accessToken;
	c.liveChatId = liveChatId;
	c.channel = account.login;
	c.assumedCost = account.streamCost;
	c.pageToken = pageToken;
	c.primed = primed;
	c.shouldAbort = [this]() {
		if (abortStream)
			return true;
		/* Also check stopping directly: a stop() racing with the reset below must not be lost. */
		std::lock_guard lock(mutex);
		return stopping;
	};
	{
		std::lock_guard lock(mutex);
		abortStream = false;
		activeKey = configKey(ConfigStore::instance().section("youtube"));
	}

	c.run();
	pageToken = c.pageToken;
	primed = c.primed;

	qint64 now = nowMs();
	if (c.endReason != "budget") {
		logStreamConnection(c, Quota::instance().used());
		connectionTimes.push_back(now);
		streamMsThisHour += c.durationMs;
	}
	while (!connectionTimes.empty() && now - connectionTimes.front() > 3600 * 1000LL)
		connectionTimes.pop_front();
	if (connectionTimes.empty())
		streamMsThisHour = 0;

	if (c.endReason == "aborted") {
		nextPoll = now; /* stop or settings change; the loop decides what's next */
		return;
	}
	if (c.quotaExceeded()) {
		pausedDay = Quota::currentDay();
		return;
	}
	if (c.unauthorized()) {
		account.expiresAt = 0;
		nextPoll = now;
		return;
	}
	if (c.chatEnded()) {
		obs_log(LOG_INFO, "YouTube live chat ended (%s)", c.reason.toUtf8().constData());
		resetChat();
		nextBroadcastCheck = now + kErrorRetryMs;
		return;
	}
	if (c.endpointUnavailable()) {
		if (++streamFailures >= 3) {
			streamFallback = true;
			streamFallbackReason = c.error.isEmpty() ? QStringLiteral("HTTP %1").arg(c.httpStatus)
								 : c.error;
			obs_log(LOG_WARNING, "YouTube streaming chat unavailable (%s), falling back to polling",
				streamFallbackReason.toUtf8().constData());
		}
		nextPoll = now + 5000;
		return;
	}
	if (c.endReason == "error" || c.endReason == "idle") {
		if (!c.error.isEmpty())
			error = c.error;
		nextPoll = now + streamBackoff * 1000LL;
		streamBackoff = std::min(streamBackoff * 2, 60);
		return;
	}

	/* Clean end of stream: resume from nextPageToken. Back off if the server keeps closing
	 * streams right away, so a misbehaving endpoint can't burn the quota. */
	error.clear();
	streamFailures = 0;
	streamBackoff = 1;
	shortStreak = c.durationMs < 5000 ? shortStreak + 1 : 0;
	nextPoll = now + (shortStreak > 0 ? std::min<qint64>(1000LL << std::min(shortStreak, 5), 30000) : 250);
}

void YouTubeProvider::updateStatus()
{
	ProviderStatus::State state;
	QString message;
	QString quota = QStringLiteral("Quota today: %1 / %2 units")
				.arg(Quota::instance().used())
				.arg(Quota::instance().limit());

	if (!account.loggedIn()) {
		state = error.isEmpty() ? ProviderStatus::State::Disabled : ProviderStatus::State::Error;
		if (!error.isEmpty())
			message = error;
		else if (account.clientId.isEmpty() || account.clientSecret.isEmpty())
			message = QStringLiteral("Add your Google OAuth client under Advanced, then log in");
		else
			message = QStringLiteral("Not logged in");
	} else if (!pausedDay.isEmpty()) {
		state = ProviderStatus::State::Error;
		message = QStringLiteral("Daily quota budget used up; chat resumes after midnight Pacific time. ") +
			  quota;
	} else if (!error.isEmpty()) {
		state = ProviderStatus::State::Error;
		message = error;
	} else if (liveChatId.isEmpty()) {
		state = ProviderStatus::State::Connecting;
		message = QStringLiteral("Logged in as %1 · waiting for a live broadcast · %2")
				  .arg(account.displayName, quota);
	} else {
		state = ProviderStatus::State::Connected;
		QString transport;
		if (account.streaming && streamFallback)
			transport = QStringLiteral(" (polling: streaming unavailable, %1)").arg(streamFallbackReason);
		else if (account.streaming && !connectionTimes.empty())
			transport = QStringLiteral(" (streaming: %1 connection(s) in the last hour, avg %2 s)")
					    .arg(connectionTimes.size())
					    .arg(streamMsThisHour / 1000 / (qint64)connectionTimes.size());
		else if (account.streaming)
			transport = QStringLiteral(" (streaming)");
		message = QStringLiteral("Logged in as %1 · live chat: %2%3 · %4")
				  .arg(account.displayName, broadcastTitle, transport, quota);
	}

	if (state != lastState || message != lastMessage) {
		lastState = state;
		lastMessage = message;
		reportStatus(state, message);
	}
}

} // namespace sf::youtube
