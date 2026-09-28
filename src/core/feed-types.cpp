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

#include "core/feed-types.hpp"

#include <QDateTime>
#include <QUuid>

namespace sf {

const QStringList &allEventTypes()
{
	static const QStringList types = {EventType::Follow,     EventType::Subscription, EventType::GiftSub,
					  EventType::Cheer,      EventType::Tip,          EventType::Raid,
					  EventType::Redemption, EventType::HypeTrain,    EventType::Membership,
					  EventType::SuperChat,  EventType::Gift};
	return types;
}

QString eventTypeLabel(const QString &type)
{
	if (type == EventType::Follow)
		return QStringLiteral("Follow");
	if (type == EventType::Subscription)
		return QStringLiteral("Subscription");
	if (type == EventType::GiftSub)
		return QStringLiteral("Gifted Subs");
	if (type == EventType::Cheer)
		return QStringLiteral("Cheer / Bits");
	if (type == EventType::Tip)
		return QStringLiteral("Tip / Donation");
	if (type == EventType::Raid)
		return QStringLiteral("Raid");
	if (type == EventType::Redemption)
		return QStringLiteral("Channel Points Redemption");
	if (type == EventType::HypeTrain)
		return QStringLiteral("Hype Train");
	if (type == EventType::Membership)
		return QStringLiteral("Membership");
	if (type == EventType::SuperChat)
		return QStringLiteral("Super Chat / Sticker");
	if (type == EventType::Gift)
		return QStringLiteral("Gift");
	return type;
}

const QStringList &allPlatforms()
{
	static const QStringList platforms = {"twitch",         "youtube",    "kick",       "tiktok",
					      "streamelements", "streamlabs", "streamerbot"};
	return platforms;
}

QString platformLabel(const QString &platform)
{
	if (platform == "twitch")
		return QStringLiteral("Twitch");
	if (platform == "youtube")
		return QStringLiteral("YouTube");
	if (platform == "kick")
		return QStringLiteral("Kick");
	if (platform == "tiktok")
		return QStringLiteral("TikTok");
	if (platform == "streamelements")
		return QStringLiteral("StreamElements");
	if (platform == "streamlabs")
		return QStringLiteral("Streamlabs");
	if (platform == "streamerbot")
		return QStringLiteral("Streamer.bot");
	return platform;
}

QString kindName(FeedKind kind)
{
	switch (kind) {
	case FeedKind::Chat:
		return QStringLiteral("chat");
	case FeedKind::Event:
		return QStringLiteral("event");
	case FeedKind::Moderation:
		return QStringLiteral("moderation");
	}
	return QStringLiteral("chat");
}

QString makeId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

qint64 nowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

QJsonObject textFragment(const QString &text)
{
	return QJsonObject{{"type", "text"}, {"text", text}};
}

QJsonObject emoteFragment(const QString &name, const QString &url, const QString &id)
{
	return QJsonObject{{"type", "emote"}, {"text", name}, {"url", url}, {"id", id}};
}

QJsonObject toEnvelope(const FeedItem &item)
{
	return QJsonObject{{"type", kindName(item.kind)}, {"payload", item.payload}};
}

static FeedItem finish(FeedKind kind, QJsonObject payload)
{
	if (payload.value("id").toString().isEmpty())
		payload["id"] = makeId();
	if (!payload.contains("timestamp"))
		payload["timestamp"] = nowMs();

	FeedItem item;
	item.kind = kind;
	item.id = payload.value("id").toString();
	item.platform = payload.value("platform").toString();
	item.channel = payload.value("channel").toString().toLower();
	item.payload = payload;
	return item;
}

FeedItem makeChatItem(QJsonObject payload)
{
	return finish(FeedKind::Chat, std::move(payload));
}

FeedItem makeEventItem(QJsonObject payload)
{
	return finish(FeedKind::Event, std::move(payload));
}

FeedItem makeModerationItem(const QString &platform, const QString &channel, const QString &action,
			    const QString &targetId)
{
	QJsonObject payload{{"platform", platform}, {"channel", channel}, {"action", action}, {"target", targetId}};
	return finish(FeedKind::Moderation, std::move(payload));
}

} // namespace sf
