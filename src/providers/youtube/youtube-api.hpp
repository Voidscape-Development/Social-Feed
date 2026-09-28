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

#include "net/http-client.hpp"
#include "providers/oauth-device.hpp"

#include <QJsonObject>
#include <QString>

#include <mutex>

namespace sf::youtube {

/* YouTube Data API v3 through the streamer's own Google Cloud project: an OAuth client of type
 * "TVs and Limited Input devices" (device code login) with the YouTube Data API enabled. The
 * quota belongs to that project, so the plugin tracks and budgets it. All calls block. */

extern const char *const kScope;

/* Quota costs of the calls we make (YouTube Data API quota table). */
inline constexpr int kCostList = 1;         /* channels.list, liveBroadcasts.list */
inline constexpr int kCostChatMessages = 5; /* liveChatMessages.list */
inline constexpr int kDefaultDailyQuota = 10000;
inline constexpr int kDefaultPollSeconds = 8;

struct AppCredentials {
	QString clientId;
	QString clientSecret;

	bool valid() const { return !clientId.isEmpty() && !clientSecret.isEmpty(); }
};

/* Base URL of the Data API (the "apiBaseUrl" config key overrides it for testing). */
QString apiBase(bool streaming = false);

/* From the "youtube" config section. */
AppCredentials appCredentials();

oauth::DeviceCode requestDeviceCode(const AppCredentials &app);
oauth::TokenResult pollDeviceToken(const AppCredentials &app, const QString &deviceCode);
oauth::TokenResult refreshToken(const AppCredentials &app, const QString &refreshToken);
void revokeToken(const QString &token);

struct ApiResult {
	net::HttpResponse http;
	QJsonObject json;
	QString reason;               /* Google error reason, e.g. quotaExceeded, liveChatEnded */
	bool budgetExhausted = false; /* our own daily budget stopped the call */

	bool ok() const { return !budgetExhausted && http.ok(); }
	bool unauthorized() const { return http.status == 401; }
	bool quotaExceeded() const
	{
		return budgetExhausted || reason == "quotaExceeded" || reason == "dailyLimitExceeded";
	}
	QString describe() const;
};

/* GET <api base>/<pathAndQuery>, charging `cost` units to the daily budget first. */
ApiResult get(const QString &accessToken, const QString &pathAndQuery, int cost);

struct ChannelInfo {
	bool ok = false;
	QString error;
	QString id;
	QString title;
	QString handle; /* without "@", lowercase; falls back to the channel id */
	QString avatar;
};

ChannelInfo fetchOwnChannel(const QString &accessToken);

/* Persists a successful login into the "youtube" config section. */
void storeLogin(const oauth::TokenResult &token, const ChannelInfo &channel);
void clearLogin();

/* Daily quota bookkeeping. YouTube quota days reset at midnight Pacific time. Usage is an
 * estimate of what this plugin spent; other apps using the same project are not counted. */
class Quota {
public:
	static Quota &instance();

	bool tryConsume(int units);
	int used();
	int limit();
	/* Writes usage to the config (throttled unless force). */
	void persist(bool force = false);
	static QString currentDay();

private:
	Quota() = default;
	void rollover();

	std::mutex mutex;
	bool loaded = false;
	QString day;
	int usedUnits = 0;
	qint64 lastSavedMs = 0;
	bool dirty = false;
};

} // namespace sf::youtube
