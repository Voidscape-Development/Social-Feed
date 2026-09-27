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

#include "providers/twitch/twitch-auth.hpp"

#include "core/config-store.hpp"
#include "core/feed-types.hpp"
#include "net/http-client.hpp"

#include <QJsonArray>

#include <algorithm>

namespace sf::twitch {

const char *const kScopes = "chat:read user:read:chat moderator:read:followers channel:read:subscriptions bits:read "
			    "channel:read:redemptions channel:read:hype_train";

QString clientId()
{
	QString configured = ConfigStore::instance().section("twitch").value("clientId").toString().trimmed();
	if (!configured.isEmpty())
		return configured;
	return QString::fromUtf8(SOCIAL_FEED_TWITCH_CLIENT_ID).trimmed();
}

static QStringList toStringList(const QJsonValue &value)
{
	QStringList out;
	for (const auto &v : value.toArray())
		out.append(v.toString());
	return out;
}

DeviceCode requestDeviceCode(const QString &clientId)
{
	DeviceCode result;
	auto response = net::request("POST", "https://id.twitch.tv/oauth2/device",
				     {"Content-Type: application/x-www-form-urlencoded"},
				     net::formEncode({{"client_id", clientId.toStdString()}, {"scopes", kScopes}}));
	if (!response.ok()) {
		result.error = QString::fromStdString(response.describe());
		return result;
	}

	QJsonObject json = response.json();
	result.ok = true;
	result.deviceCode = json.value("device_code").toString();
	result.userCode = json.value("user_code").toString();
	result.verificationUri = json.value("verification_uri").toString();
	result.intervalSeconds = std::max(1, json.value("interval").toInt(5));
	result.expiresInSeconds = json.value("expires_in").toInt(1800);
	return result;
}

static TokenResult parseToken(const net::HttpResponse &response)
{
	TokenResult result;
	QJsonObject json = response.json();
	if (response.ok()) {
		result.status = TokenResult::Status::Success;
		result.accessToken = json.value("access_token").toString();
		result.refreshToken = json.value("refresh_token").toString();
		result.expiresInSeconds = json.value("expires_in").toInteger();
		result.scopes = toStringList(json.value("scope"));
		return result;
	}

	QString message = json.value("message").toString();
	if (message == "authorization_pending")
		result.status = TokenResult::Status::Pending;
	else if (message == "slow_down")
		result.status = TokenResult::Status::SlowDown;
	else if (message == "access_denied")
		result.status = TokenResult::Status::Denied;
	else if (message == "expired_token" || message == "invalid device code")
		result.status = TokenResult::Status::Expired;
	else
		result.status = TokenResult::Status::Error;
	result.error = message.isEmpty() ? QString::fromStdString(response.describe()) : message;
	return result;
}

TokenResult pollDeviceToken(const QString &clientId, const QString &deviceCode)
{
	auto response = net::request("POST", "https://id.twitch.tv/oauth2/token",
				     {"Content-Type: application/x-www-form-urlencoded"},
				     net::formEncode({{"client_id", clientId.toStdString()},
						      {"scopes", kScopes},
						      {"device_code", deviceCode.toStdString()},
						      {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}}));
	return parseToken(response);
}

TokenResult refreshToken(const QString &clientId, const QString &refreshToken)
{
	auto response = net::request("POST", "https://id.twitch.tv/oauth2/token",
				     {"Content-Type: application/x-www-form-urlencoded"},
				     net::formEncode({{"client_id", clientId.toStdString()},
						      {"grant_type", "refresh_token"},
						      {"refresh_token", refreshToken.toStdString()}}));
	return parseToken(response);
}

ValidateResult validateToken(const QString &accessToken)
{
	ValidateResult result;
	auto response =
		net::get("https://id.twitch.tv/oauth2/validate", {"Authorization: OAuth " + accessToken.toStdString()});
	if (!response.error.empty()) {
		result.error = QString::fromStdString(response.error);
		return result;
	}
	if (response.status == 401) {
		result.unauthorized = true;
		result.error = QStringLiteral("token is invalid or expired");
		return result;
	}
	if (!response.ok()) {
		result.error = QString::fromStdString(response.describe());
		return result;
	}

	QJsonObject json = response.json();
	result.ok = true;
	result.login = json.value("login").toString();
	result.userId = json.value("user_id").toString();
	result.scopes = toStringList(json.value("scopes"));
	result.expiresInSeconds = json.value("expires_in").toInteger();
	return result;
}

void revokeToken(const QString &clientId, const QString &accessToken)
{
	net::request("POST", "https://id.twitch.tv/oauth2/revoke", {"Content-Type: application/x-www-form-urlencoded"},
		     net::formEncode({{"client_id", clientId.toStdString()}, {"token", accessToken.toStdString()}}));
}

void storeLogin(const TokenResult &token, const ValidateResult &identity)
{
	QJsonObject changes{{"accessToken", token.accessToken},
			    {"refreshToken", token.refreshToken},
			    {"expiresAt", nowMs() + token.expiresInSeconds * 1000},
			    {"scopes", QJsonArray::fromStringList(token.scopes)}};
	if (identity.ok) {
		changes["login"] = identity.login;
		changes["userId"] = identity.userId;
	}
	ConfigStore::instance().updateSection("twitch", changes);
}

void clearLogin()
{
	QJsonObject section = ConfigStore::instance().section("twitch");
	for (const char *key : {"accessToken", "refreshToken", "expiresAt", "scopes", "login", "userId", "displayName"})
		section.remove(key);
	ConfigStore::instance().setSection("twitch", section);
}

} // namespace sf::twitch
