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

#include "providers/youtube/youtube-chat-parser.hpp"

#include <QDateTime>
#include <QJsonObject>

#include <algorithm>

namespace sf::youtube {

namespace {

QJsonObject buildUser(const QJsonObject &author)
{
	QJsonArray roles;
	if (author.value("isChatOwner").toBool())
		roles.append("broadcaster");
	if (author.value("isChatModerator").toBool())
		roles.append("moderator");
	if (author.value("isChatSponsor").toBool())
		roles.append("subscriber");

	QString displayName = author.value("displayName").toString();
	QString login = displayName.startsWith('@') ? displayName.mid(1) : displayName;
	return QJsonObject{{"id", author.value("channelId").toString()},
			   {"login", login.toLower()},
			   {"displayName", displayName},
			   {"avatar", author.value("profileImageUrl").toString()},
			   {"roles", roles},
			   {"badges", QJsonArray()}};
}

qint64 timestampOf(const QJsonObject &snippet)
{
	QDateTime published = QDateTime::fromString(snippet.value("publishedAt").toString(), Qt::ISODateWithMs);
	return published.isValid() ? published.toMSecsSinceEpoch() : nowMs();
}

double micros(const QJsonValue &value)
{
	/* amountMicros is a string in the API ("5000000"). */
	return (value.isString() ? value.toString().toDouble() : value.toDouble()) / 1000000.0;
}

} // namespace

ParseResult parseChatMessages(const QJsonArray &items, const QString &channel)
{
	ParseResult result;

	for (const QJsonValue value : items) {
		QJsonObject item = value.toObject();
		QJsonObject snippet = item.value("snippet").toObject();
		QJsonObject user = buildUser(item.value("authorDetails").toObject());
		QString type = snippet.value("type").toString();
		QString id = item.value("id").toString();
		qint64 timestamp = timestampOf(snippet);

		auto chat = [&](const QString &text, const QString &paid = QString()) {
			QJsonObject payload{{"id", id},
					    {"platform", "youtube"},
					    {"channel", channel},
					    {"user", user},
					    {"text", text},
					    {"fragments", QJsonArray{textFragment(text)}},
					    {"firstMessage", false},
					    {"isAction", false},
					    {"highlighted", !paid.isEmpty()},
					    {"timestamp", timestamp}};
			if (!paid.isEmpty())
				payload["paid"] = paid;
			result.items.push_back(makeChatItem(payload));
		};
		auto event = [&](QJsonObject payload) {
			payload["id"] = id + ":event";
			payload["platform"] = "youtube";
			payload["channel"] = channel;
			payload["user"] = user;
			payload["timestamp"] = timestamp;
			result.items.push_back(makeEventItem(payload));
		};

		if (type == "textMessageEvent") {
			chat(snippet.value("textMessageDetails")
				     .toObject()
				     .value("messageText")
				     .toString(snippet.value("displayMessage").toString()));
		} else if (type == "superChatEvent") {
			QJsonObject d = snippet.value("superChatDetails").toObject();
			QString display = d.value("amountDisplayString").toString();
			QString comment = d.value("userComment").toString();
			event({{"type", "super_chat"},
			       {"amount", micros(d.value("amountMicros"))},
			       {"currency", d.value("currency").toString()},
			       {"formattedAmount", display},
			       {"tier", QString::number(d.value("tier").toInt())},
			       {"message", comment}});
			chat(comment, display);
		} else if (type == "superStickerEvent") {
			QJsonObject d = snippet.value("superStickerDetails").toObject();
			QString sticker = d.value("superStickerMetadata").toObject().value("altText").toString();
			QString display = d.value("amountDisplayString").toString();
			event({{"type", "super_chat"},
			       {"amount", micros(d.value("amountMicros"))},
			       {"currency", d.value("currency").toString()},
			       {"formattedAmount", display},
			       {"tier", QString::number(d.value("tier").toInt())},
			       {"reward", sticker},
			       {"sticker", true},
			       {"message", QString()}});
			chat(sticker.isEmpty() ? QStringLiteral("[Super Sticker]")
					       : QStringLiteral("[%1]").arg(sticker),
			     display);
		} else if (type == "newSponsorEvent") {
			QJsonObject d = snippet.value("newSponsorDetails").toObject();
			event({{"type", "membership"},
			       {"tier", d.value("memberLevelName").toString()},
			       {"months", 1},
			       {"amount", 1},
			       {"upgrade", d.value("isUpgrade").toBool()},
			       {"message", QString()}});
		} else if (type == "memberMilestoneChatEvent") {
			QJsonObject d = snippet.value("memberMilestoneChatDetails").toObject();
			int months = std::max(1, d.value("memberMonth").toInt());
			QString comment = d.value("userComment").toString();
			event({{"type", "membership"},
			       {"tier", d.value("memberLevelName").toString()},
			       {"months", months},
			       {"amount", months},
			       {"message", comment}});
			if (!comment.isEmpty())
				chat(comment);
		} else if (type == "membershipGiftingEvent") {
			QJsonObject d = snippet.value("membershipGiftingDetails").toObject();
			int count = std::max(1, d.value("giftMembershipsCount").toInt());
			event({{"type", "gift_sub"},
			       {"tier", d.value("giftMembershipsLevelName").toString()},
			       {"count", count},
			       {"amount", count},
			       {"message", QString()}});
		} else if (type == "giftEvent") {
			/* YouTube "Jewels" gifts. amount = total jewels, so amount variants scale with value. */
			QJsonObject d = snippet.value("giftDetails").toObject();
			int combo = std::max(1, d.value("comboCount").toInt(1));
			int jewels = d.value("jewelsAmount").toInt() * combo;
			QString name = d.value("giftName").toString();
			event({{"type", "gift"},
			       {"reward", name.isEmpty() ? d.value("altText").toString() : name},
			       {"count", combo},
			       {"amount", jewels},
			       {"formattedAmount", QStringLiteral("%1 jewels").arg(jewels)},
			       {"message", QString()}});
		} else if (type == "messageDeletedEvent") {
			QString target =
				snippet.value("messageDeletedDetails").toObject().value("deletedMessageId").toString();
			result.items.push_back(makeModerationItem("youtube", channel, "delete_message", target));
		} else if (type == "userBannedEvent") {
			QString target = snippet.value("userBannedDetails")
						 .toObject()
						 .value("bannedUserDetails")
						 .toObject()
						 .value("channelId")
						 .toString();
			result.items.push_back(makeModerationItem("youtube", channel, "clear_user", target));
		} else if (type == "chatEndedEvent") {
			result.chatEnded = true;
		}
		/* Other types (giftMembershipReceivedEvent, sponsor-only mode, polls, tombstones, …) are
		 * ignored. Google no longer delivers messageDeletedEvent; it is kept for older data. */
	}
	return result;
}

} // namespace sf::youtube
