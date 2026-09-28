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

#include <QString>
#include <QStringList>

namespace sf::oauth {

/* Types shared by the OAuth 2.0 Device Authorization Grant (RFC 8628) implementations. */

struct DeviceCode {
	bool ok = false;
	QString error;
	QString deviceCode;
	QString userCode;
	QString verificationUri;
	int intervalSeconds = 5;
	int expiresInSeconds = 1800;
};

struct TokenResult {
	enum class Status { Success, Pending, SlowDown, Denied, Expired, Error };

	Status status = Status::Error;
	QString error;
	QString accessToken;
	QString refreshToken;
	qint64 expiresInSeconds = 0;
	QStringList scopes;
};

/* Maps the RFC 8628 error codes onto TokenResult::Status. */
inline TokenResult::Status statusForError(const QString &error)
{
	if (error == "authorization_pending")
		return TokenResult::Status::Pending;
	if (error == "slow_down")
		return TokenResult::Status::SlowDown;
	if (error == "access_denied")
		return TokenResult::Status::Denied;
	if (error == "expired_token" || error == "invalid device code")
		return TokenResult::Status::Expired;
	return TokenResult::Status::Error;
}

} // namespace sf::oauth
