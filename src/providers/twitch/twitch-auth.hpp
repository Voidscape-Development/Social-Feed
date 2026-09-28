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

#include "providers/oauth-device.hpp"

#include <QString>
#include <QStringList>

namespace sf::twitch {

/* Twitch OAuth using the Device Code Grant flow, which needs no client secret and no redirect
 * listener, as suited to a distributed desktop plugin. All functions block (HTTP). */

extern const char *const kScopes;

/* Client ID from the Accounts dialog override, falling back to the build-time
 * SOCIAL_FEED_TWITCH_CLIENT_ID. Empty when neither is set. */
QString clientId();

using DeviceCode = oauth::DeviceCode;
using TokenResult = oauth::TokenResult;

struct ValidateResult {
	bool ok = false;
	bool unauthorized = false; /* token is invalid (as opposed to a network error) */
	QString error;
	QString login;
	QString userId;
	QStringList scopes;
	qint64 expiresInSeconds = 0;
};

DeviceCode requestDeviceCode(const QString &clientId);
TokenResult pollDeviceToken(const QString &clientId, const QString &deviceCode);
TokenResult refreshToken(const QString &clientId, const QString &refreshToken);
ValidateResult validateToken(const QString &accessToken);
void revokeToken(const QString &clientId, const QString &accessToken);

/* Persists a successful login into the "twitch" config section. */
void storeLogin(const TokenResult &token, const ValidateResult &identity);
void clearLogin();

} // namespace sf::twitch
