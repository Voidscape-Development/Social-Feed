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

#include "providers/youtube/youtube-api.hpp"

#include "core/config-store.hpp"
#include "core/feed-types.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QTimeZone>

#include <algorithm>

namespace sf::youtube {

const char *const kScope = "https://www.googleapis.com/auth/youtube.readonly";

namespace {

constexpr const char *kDeviceCodeUrl = "https://oauth2.googleapis.com/device/code";
constexpr const char *kTokenUrl = "https://oauth2.googleapis.com/token";
constexpr const char *kRevokeUrl = "https://oauth2.googleapis.com/revoke";
constexpr const char *kQuotaSection = "youtube_quota";

oauth::TokenResult parseToken(const net::HttpResponse &response)
{
	oauth::TokenResult result;
	QJsonObject json = response.json();
	if (response.ok()) {
		result.status = oauth::TokenResult::Status::Success;
		result.accessToken = json.value("access_token").toString();
		result.refreshToken = json.value("refresh_token").toString();
		result.expiresInSeconds = json.value("expires_in").toInteger();
		result.scopes = json.value("scope").toString().split(' ', Qt::SkipEmptyParts);
		return result;
	}
	QString error = json.value("error").toString();
	/* invalid_grant: the refresh token was revoked/expired, or the device code is unusable. */
	result.status = error == "invalid_grant" ? oauth::TokenResult::Status::Expired : oauth::statusForError(error);
	QString description = json.value("error_description").toString();
	result.error = description.isEmpty() ? (error.isEmpty() ? QString::fromStdString(response.describe()) : error)
					     : description;
	return result;
}

} // namespace

QString apiBase(bool streaming)
{
	/* Development override, used to point the provider at a mock server. */
	QString base = ConfigStore::instance().section("youtube").value("apiBaseUrl").toString().trimmed();
	if (!base.isEmpty())
		return base;
	/* The streaming method is published on the youtube.googleapis.com root (discovery rootUrl). */
	return streaming ? QStringLiteral("https://youtube.googleapis.com/youtube/v3")
			 : QStringLiteral("https://www.googleapis.com/youtube/v3");
}

AppCredentials appCredentials()
{
	QJsonObject section = ConfigStore::instance().section("youtube");
	return {section.value("clientId").toString().trimmed(), section.value("clientSecret").toString().trimmed()};
}

oauth::DeviceCode requestDeviceCode(const AppCredentials &app)
{
	oauth::DeviceCode result;
	auto response = net::request("POST", kDeviceCodeUrl, {"Content-Type: application/x-www-form-urlencoded"},
				     net::formEncode({{"client_id", app.clientId.toStdString()}, {"scope", kScope}}));
	QJsonObject json = response.json();
	if (!response.ok()) {
		QString error = json.value("error_description").toString();
		result.error = error.isEmpty() ? QString::fromStdString(response.describe()) : error;
		return result;
	}
	result.ok = true;
	result.deviceCode = json.value("device_code").toString();
	result.userCode = json.value("user_code").toString();
	result.verificationUri = json.value("verification_url").toString();
	if (result.verificationUri.isEmpty())
		result.verificationUri = json.value("verification_uri").toString();
	result.intervalSeconds = std::max(1, json.value("interval").toInt(5));
	result.expiresInSeconds = json.value("expires_in").toInt(1800);
	return result;
}

oauth::TokenResult pollDeviceToken(const AppCredentials &app, const QString &deviceCode)
{
	return parseToken(
		net::request("POST", kTokenUrl, {"Content-Type: application/x-www-form-urlencoded"},
			     net::formEncode({{"client_id", app.clientId.toStdString()},
					      {"client_secret", app.clientSecret.toStdString()},
					      {"device_code", deviceCode.toStdString()},
					      {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}})));
}

oauth::TokenResult refreshToken(const AppCredentials &app, const QString &refresh)
{
	oauth::TokenResult result =
		parseToken(net::request("POST", kTokenUrl, {"Content-Type: application/x-www-form-urlencoded"},
					net::formEncode({{"client_id", app.clientId.toStdString()},
							 {"client_secret", app.clientSecret.toStdString()},
							 {"refresh_token", refresh.toStdString()},
							 {"grant_type", "refresh_token"}})));
	/* Google does not rotate refresh tokens on refresh. */
	if (result.status == oauth::TokenResult::Status::Success && result.refreshToken.isEmpty())
		result.refreshToken = refresh;
	return result;
}

void revokeToken(const QString &token)
{
	net::request("POST", kRevokeUrl, {"Content-Type: application/x-www-form-urlencoded"},
		     net::formEncode({{"token", token.toStdString()}}));
}

QString ApiResult::describe() const
{
	if (budgetExhausted)
		return QStringLiteral("daily quota budget reached");
	QString message = json.value("error").toObject().value("message").toString();
	if (!message.isEmpty())
		return message;
	return QString::fromStdString(http.describe());
}

ApiResult get(const QString &accessToken, const QString &pathAndQuery, int cost)
{
	ApiResult result;
	if (!Quota::instance().tryConsume(cost)) {
		result.budgetExhausted = true;
		return result;
	}

	result.http = net::get((apiBase() + "/" + pathAndQuery).toStdString(),
			       {"Authorization: Bearer " + accessToken.toStdString(), "Accept: application/json"});
	result.json = result.http.json();
	if (!result.http.ok()) {
		QJsonObject error = result.json.value("error").toObject();
		QJsonArray errors = error.value("errors").toArray();
		if (!errors.isEmpty())
			result.reason = errors.first().toObject().value("reason").toString();
	}
	return result;
}

ChannelInfo fetchOwnChannel(const QString &accessToken)
{
	ChannelInfo info;
	ApiResult result = get(accessToken, "channels?part=snippet&mine=true", kCostList);
	if (!result.ok()) {
		info.error = result.describe();
		return info;
	}
	QJsonArray items = result.json.value("items").toArray();
	if (items.isEmpty()) {
		info.error = QStringLiteral("This Google account has no YouTube channel");
		return info;
	}
	QJsonObject item = items.first().toObject();
	QJsonObject snippet = item.value("snippet").toObject();
	info.ok = true;
	info.id = item.value("id").toString();
	info.title = snippet.value("title").toString();
	info.handle = snippet.value("customUrl").toString().toLower();
	if (info.handle.startsWith('@'))
		info.handle = info.handle.mid(1);
	if (info.handle.isEmpty())
		info.handle = info.id.toLower();
	info.avatar = snippet.value("thumbnails").toObject().value("default").toObject().value("url").toString();
	return info;
}

void storeLogin(const oauth::TokenResult &token, const ChannelInfo &channel)
{
	ConfigStore::instance().updateSection("youtube", {{"accessToken", token.accessToken},
							  {"refreshToken", token.refreshToken},
							  {"expiresAt", nowMs() + token.expiresInSeconds * 1000},
							  {"channelId", channel.id},
							  {"login", channel.handle},
							  {"displayName", channel.title}});
}

void clearLogin()
{
	QJsonObject section = ConfigStore::instance().section("youtube");
	for (const char *key : {"accessToken", "refreshToken", "expiresAt", "channelId", "login", "displayName"})
		section.remove(key);
	ConfigStore::instance().setSection("youtube", section);
}

/* ---- Quota ---- */

Quota &Quota::instance()
{
	static Quota quota;
	return quota;
}

QString Quota::currentDay()
{
	QDateTime utc = QDateTime::currentDateTimeUtc();
	QTimeZone pacific("America/Los_Angeles");
	QDateTime local = pacific.isValid() ? utc.toTimeZone(pacific) : utc.addSecs(-8 * 3600);
	return local.date().toString(Qt::ISODate);
}

void Quota::rollover()
{
	if (!loaded) {
		QJsonObject saved = ConfigStore::instance().section(kQuotaSection);
		day = saved.value("day").toString();
		usedUnits = saved.value("used").toInt();
		loaded = true;
	}
	QString today = currentDay();
	if (day != today) {
		day = today;
		usedUnits = 0;
		dirty = true;
	}
}

bool Quota::tryConsume(int units)
{
	std::lock_guard lock(mutex);
	rollover();
	int budget = ConfigStore::instance().section("youtube").value("dailyQuota").toInt(kDefaultDailyQuota);
	if (usedUnits + units > budget)
		return false;
	usedUnits += units;
	dirty = true;
	return true;
}

int Quota::used()
{
	std::lock_guard lock(mutex);
	rollover();
	return usedUnits;
}

int Quota::limit()
{
	return ConfigStore::instance().section("youtube").value("dailyQuota").toInt(kDefaultDailyQuota);
}

void Quota::persist(bool force)
{
	QJsonObject value;
	{
		std::lock_guard lock(mutex);
		rollover();
		if (!dirty || (!force && nowMs() - lastSavedMs < 60 * 1000))
			return;
		dirty = false;
		lastSavedMs = nowMs();
		value = QJsonObject{{"day", day}, {"used", usedUnits}};
	}
	/* A separate section, so saving usage does not look like an account change. */
	ConfigStore::instance().setSection(kQuotaSection, value);
}

} // namespace sf::youtube
