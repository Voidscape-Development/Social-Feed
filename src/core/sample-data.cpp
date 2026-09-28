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

#include "core/sample-data.hpp"

#include <QRandomGenerator>

#include <iterator>

namespace sf {

namespace {

struct SampleUser {
	const char *name;
	const char *color;
	const char *role;
};

const SampleUser kUsers[] = {
	{"PixelPanda", "#FF7F50", "subscriber"}, {"NightOwl_42", "#1E90FF", ""},
	{"ModMaverick", "#00C853", "moderator"}, {"VIPVelvet", "#E040FB", "vip"},
	{"cozy_coder", "#FFD600", ""},           {"GlitchGoblin", "#FF1744", "subscriber"},
};

const char *kMessages[] = {
	"Hello chat! Kappa",
	"That play was insane PogChamp",
	"first time here, love the overlay!",
	"!discord",
	"GG everyone, see you next stream",
	"@Streamer what settings are you using?",
	"LUL LUL LUL",
	"This song is a vibe",
};

const char *kPlatforms[] = {"twitch", "youtube", "kick", "tiktok"};

int pick(int count)
{
	return (int)QRandomGenerator::global()->bounded(count);
}

QJsonObject sampleUser(const SampleUser &u)
{
	QJsonArray roles;
	if (*u.role)
		roles.append(u.role);
	return QJsonObject{{"id", QString::number(qHash(QString(u.name)))},
			   {"login", QString(u.name).toLower()},
			   {"displayName", u.name},
			   {"color", u.color},
			   {"roles", roles},
			   {"badges", QJsonArray()}};
}

QString resolvePlatform(const QString &platform)
{
	return platform.isEmpty() ? QString(kPlatforms[pick(4)]) : platform;
}

} // namespace

FeedItem sampleChatMessage(const QString &platform)
{
	const SampleUser &user = kUsers[pick(std::size(kUsers))];
	QString text = kMessages[pick(std::size(kMessages))];

	QJsonArray fragments;
	for (const QString &word : text.split(' ')) {
		if (!fragments.isEmpty())
			fragments.append(textFragment(" "));
		if (word == "Kappa")
			fragments.append(emoteFragment(
				word, "https://static-cdn.jtvnw.net/emoticons/v2/25/default/dark/2.0", "25"));
		else if (word == "LUL")
			fragments.append(emoteFragment(
				word, "https://static-cdn.jtvnw.net/emoticons/v2/425618/default/dark/2.0", "425618"));
		else
			fragments.append(textFragment(word));
	}

	QJsonObject payload{{"platform", resolvePlatform(platform)},
			    {"channel", "socialfeed_test"},
			    {"user", sampleUser(user)},
			    {"text", text},
			    {"fragments", fragments},
			    {"firstMessage", text.startsWith("first time")},
			    {"test", true}};
	return makeChatItem(payload);
}

FeedItem sampleEvent(const QString &type, const QString &platform)
{
	const SampleUser &user = kUsers[pick(std::size(kUsers))];
	QJsonObject payload{{"type", type},
			    {"platform", resolvePlatform(platform)},
			    {"channel", "socialfeed_test"},
			    {"user", sampleUser(user)},
			    {"test", true}};

	if (type == EventType::Subscription) {
		int months = 1 + pick(24);
		payload["platform"] = platform.isEmpty() ? "twitch" : platform;
		payload["tier"] = "1000";
		payload["months"] = months;
		payload["amount"] = months;
		payload["message"] = months > 1 ? "Happy to be here again!" : "";
	} else if (type == EventType::GiftSub) {
		int count = QList<int>{1, 5, 10, 50}[pick(4)];
		payload["platform"] = platform.isEmpty() ? "twitch" : platform;
		payload["tier"] = "1000";
		payload["amount"] = count;
		payload["count"] = count;
	} else if (type == EventType::Cheer) {
		int bits = QList<int>{100, 500, 1000, 5000}[pick(4)];
		payload["platform"] = "twitch";
		payload["amount"] = bits;
		payload["formattedAmount"] = QStringLiteral("%1 bits").arg(bits);
		payload["message"] = "Cheer100 take my bits!";
	} else if (type == EventType::Tip || type == EventType::SuperChat) {
		double amount = QList<double>{2.0, 5.0, 10.0, 25.0, 100.0}[pick(5)];
		payload["platform"] = type == EventType::Tip ? (platform.isEmpty() ? "streamelements" : platform)
							     : "youtube";
		payload["amount"] = amount;
		payload["currency"] = "USD";
		payload["formattedAmount"] = QStringLiteral("$%1").arg(amount, 0, 'f', 2);
		payload["message"] = "Keep up the great work!";
	} else if (type == EventType::Raid) {
		int viewers = 5 + pick(500);
		payload["platform"] = "twitch";
		payload["amount"] = viewers;
		payload["count"] = viewers;
	} else if (type == EventType::Redemption) {
		payload["platform"] = "twitch";
		payload["reward"] = "Hydrate!";
		payload["amount"] = 500;
		payload["formattedAmount"] = "500 points";
		payload["message"] = "";
	} else if (type == EventType::HypeTrain) {
		payload["platform"] = "twitch";
		payload["amount"] = 1 + pick(5);
		payload["level"] = payload["amount"];
	} else if (type == EventType::Membership) {
		payload["platform"] = "youtube";
		payload["tier"] = "Member";
		payload["months"] = 1 + pick(12);
		payload["amount"] = payload["months"];
	} else if (type == EventType::Gift) {
		payload["platform"] = "tiktok";
		payload["reward"] = "Rose";
		payload["count"] = 1 + pick(20);
		payload["amount"] = payload["count"];
	}

	return makeEventItem(payload);
}

} // namespace sf
