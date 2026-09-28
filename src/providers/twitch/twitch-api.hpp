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

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <mutex>

namespace sf::twitch {

struct Credentials {
	QString clientId;
	QString accessToken;

	bool valid() const { return !clientId.isEmpty() && !accessToken.isEmpty(); }
};

/* Thin Helix wrapper (blocking). */
net::HttpResponse helix(const Credentials &creds, const std::string &method, const std::string &path,
			const QJsonObject &body = {});

/* Chat badge and profile image lookups, cached. Filled by the provider's worker thread, read by
 * the chat thread. */
class TwitchCache {
public:
	struct Badge {
		QString url;
		QString title;
	};

	static TwitchCache &instance();

	/* Loads global badges once and channel badges for roomId if not loaded yet. */
	void ensureBadges(const Credentials &creds, const QString &roomId);
	bool lookupBadge(const QString &roomId, const QString &set, const QString &version, Badge &out) const;

	/* Profile image URL for a user id (blocking on first lookup). */
	QString avatar(const Credentials &creds, const QString &userId);

	void clear();

private:
	TwitchCache() = default;
	bool loadBadgeSet(const Credentials &creds, const QString &key, const std::string &path);

	mutable std::mutex mutex;
	/* key: "global" or roomId -> "set/version" -> badge */
	QHash<QString, QHash<QString, Badge>> badges;
	QSet<QString> attempted;
	QHash<QString, QString> avatars;
};

} // namespace sf::twitch
