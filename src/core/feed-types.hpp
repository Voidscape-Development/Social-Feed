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

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sf {

/* Every item that flows from a provider to the overlays is one of these kinds. The payload
 * is the normalized JSON object documented in docs/overlay-protocol.md. */
enum class FeedKind { Chat, Event, Moderation };

struct FeedItem {
	FeedKind kind = FeedKind::Chat;
	QString id;
	QString platform; /* "twitch", "youtube", "kick", "tiktok", "streamelements", ... */
	QString channel;  /* lowercase channel login the item belongs to (may be empty for tips) */
	QJsonObject payload;
};

/* Event types understood by the Event Display. */
namespace EventType {
inline constexpr const char *Follow = "follow";
inline constexpr const char *Subscription = "subscription";
inline constexpr const char *GiftSub = "gift_sub";
inline constexpr const char *Cheer = "cheer";
inline constexpr const char *Tip = "tip";
inline constexpr const char *Raid = "raid";
inline constexpr const char *Redemption = "redemption";
inline constexpr const char *HypeTrain = "hype_train";
inline constexpr const char *Membership = "membership";
inline constexpr const char *SuperChat = "super_chat";
inline constexpr const char *Gift = "gift";
} // namespace EventType

const QStringList &allEventTypes();
QString eventTypeLabel(const QString &type);

const QStringList &allPlatforms();
QString platformLabel(const QString &platform);

QString kindName(FeedKind kind);

/* Builds a unique id for items that do not carry a platform id. */
QString makeId();
qint64 nowMs();

/* Chat helpers */
QJsonObject textFragment(const QString &text);
QJsonObject emoteFragment(const QString &name, const QString &url, const QString &id);

/* Wraps a feed item into the envelope delivered to overlays: {type, payload}. */
QJsonObject toEnvelope(const FeedItem &item);

FeedItem makeChatItem(QJsonObject payload);
FeedItem makeEventItem(QJsonObject payload);
FeedItem makeModerationItem(const QString &platform, const QString &channel, const QString &action,
			    const QString &targetId);

} // namespace sf
