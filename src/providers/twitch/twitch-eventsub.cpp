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

#include "providers/twitch/twitch-eventsub.hpp"

#include "core/event-bus.hpp"
#include "core/feed-types.hpp"

#include <plugin-support.h>
#include <obs-module.h>

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace sf::twitch {

namespace {

struct Subscription {
	const char *type;
	const char *version;
	enum class Condition { Broadcaster, BroadcasterAndModerator, ToBroadcaster } condition;
};

const Subscription kSubscriptions[] = {
	{"channel.follow", "2", Subscription::Condition::BroadcasterAndModerator},
	{"channel.subscribe", "1", Subscription::Condition::Broadcaster},
	{"channel.subscription.gift", "1", Subscription::Condition::Broadcaster},
	{"channel.subscription.message", "1", Subscription::Condition::Broadcaster},
	{"channel.cheer", "1", Subscription::Condition::Broadcaster},
	{"channel.raid", "1", Subscription::Condition::ToBroadcaster},
	{"channel.channel_points_custom_reward_redemption.add", "1", Subscription::Condition::Broadcaster},
	{"channel.hype_train.begin", "2", Subscription::Condition::Broadcaster},
};

QString tierFrom(const QJsonObject &event)
{
	return event.value("tier").toString("1000");
}

QJsonObject userFrom(const QJsonObject &event, const char *prefix = "user")
{
	QString p = QString::fromLatin1(prefix);
	return QJsonObject{{"id", event.value(p + "_id").toString()},
			   {"login", event.value(p + "_login").toString()},
			   {"displayName", event.value(p + "_name").toString()},
			   {"roles", QJsonArray()},
			   {"badges", QJsonArray()}};
}

} // namespace

void TwitchEventSub::connect(const Options &opts, const std::string &url)
{
	options = opts;
	welcomed = false;
	dropped = false;
	authFailed = false;
	lastMessageMs = nowMs();
	{
		std::lock_guard lock(mutex);
		error.clear();
		reconnectUrl.clear();
	}

	net::WebSocketClient::Handlers handlers;
	handlers.onMessage = [this](const std::string &data) {
		handleMessage(data);
	};
	handlers.onClose = [this](const std::string &reason) {
		{
			std::lock_guard lock(mutex);
			if (error.isEmpty())
				error = QString::fromStdString(reason);
		}
		welcomed = false;
		dropped = true;
	};
	socket.open(url, std::move(handlers));
}

void TwitchEventSub::disconnect()
{
	socket.close();
	welcomed = false;
}

bool TwitchEventSub::needsReconnect() const
{
	if (dropped)
		return true;
	/* Twitch sends keepalives; silence beyond the timeout means the connection is dead. */
	return welcomed && nowMs() - lastMessageMs > (keepaliveSeconds + 10) * 1000LL;
}

std::string TwitchEventSub::takeReconnectUrl()
{
	std::lock_guard lock(mutex);
	std::string url;
	url.swap(reconnectUrl);
	return url;
}

QString TwitchEventSub::lastError() const
{
	std::lock_guard lock(mutex);
	return error;
}

void TwitchEventSub::handleMessage(const std::string &data)
{
	lastMessageMs = nowMs();
	QJsonObject root = QJsonDocument::fromJson(QByteArray::fromStdString(data)).object();
	QJsonObject metadata = root.value("metadata").toObject();
	QJsonObject payload = root.value("payload").toObject();
	QString messageType = metadata.value("message_type").toString();

	if (messageType == "session_welcome") {
		QJsonObject session = payload.value("session").toObject();
		keepaliveSeconds = std::max(10, session.value("keepalive_timeout_seconds").toInt(10));
		welcomed = true;
		/* After a session_reconnect Twitch migrates existing subscriptions, but creating them
		 * again only yields 409 Conflict, so always subscribing keeps the logic simple. */
		subscribeAll(session.value("id").toString());
	} else if (messageType == "session_reconnect") {
		std::lock_guard lock(mutex);
		reconnectUrl = payload.value("session").toObject().value("reconnect_url").toString().toStdString();
		dropped = true;
		socket.close();
	} else if (messageType == "notification") {
		QString type = payload.value("subscription").toObject().value("type").toString();
		handleNotification(type, payload.value("event").toObject());
	} else if (messageType == "revocation") {
		QString type = payload.value("subscription").toObject().value("type").toString();
		QString status = payload.value("subscription").toObject().value("status").toString();
		obs_log(LOG_WARNING, "Twitch EventSub subscription %s revoked: %s", type.toUtf8().constData(),
			status.toUtf8().constData());
	}
}

void TwitchEventSub::subscribeAll(const QString &sessionId)
{
	int failures = 0;
	for (const auto &sub : kSubscriptions) {
		QJsonObject condition;
		switch (sub.condition) {
		case Subscription::Condition::Broadcaster:
			condition["broadcaster_user_id"] = options.userId;
			break;
		case Subscription::Condition::BroadcasterAndModerator:
			condition["broadcaster_user_id"] = options.userId;
			condition["moderator_user_id"] = options.userId;
			break;
		case Subscription::Condition::ToBroadcaster:
			condition["to_broadcaster_user_id"] = options.userId;
			break;
		}

		QJsonObject body{{"type", sub.type},
				 {"version", sub.version},
				 {"condition", condition},
				 {"transport", QJsonObject{{"method", "websocket"}, {"session_id", sessionId}}}};
		auto response = helix(options.creds, "POST", "/eventsub/subscriptions", body);

		if (response.status == 401) {
			authFailed = true;
			std::lock_guard lock(mutex);
			error = QStringLiteral("Twitch rejected the login token");
			return;
		}
		/* hype_train.begin v2 replaced v1; fall back for accounts where v2 is unavailable. */
		if (!response.ok() && response.status != 409 && QString(sub.type) == "channel.hype_train.begin") {
			body["version"] = "1";
			response = helix(options.creds, "POST", "/eventsub/subscriptions", body);
		}
		if (!response.ok() && response.status != 409) {
			failures++;
			obs_log(LOG_WARNING, "Twitch EventSub %s failed: %s", sub.type, response.describe().c_str());
		}
	}

	if (failures > 0) {
		std::lock_guard lock(mutex);
		error = QStringLiteral("%1 event type(s) unavailable, see OBS log").arg(failures);
	}
}

void TwitchEventSub::handleNotification(const QString &type, const QJsonObject &e)
{
	QJsonObject event{{"platform", "twitch"}, {"channel", options.login.toLower()}};
	QJsonObject user = userFrom(e);
	bool anonymous = e.value("is_anonymous").toBool();

	if (type == "channel.follow") {
		event["type"] = "follow";
	} else if (type == "channel.subscribe") {
		/* Gifted subs are announced through channel.subscription.gift instead. */
		if (e.value("is_gift").toBool())
			return;
		event["type"] = "subscription";
		event["tier"] = tierFrom(e);
		event["months"] = 1;
		event["amount"] = 1;
	} else if (type == "channel.subscription.message") {
		int months = std::max(1, e.value("cumulative_months").toInt());
		event["type"] = "subscription";
		event["tier"] = tierFrom(e);
		event["months"] = months;
		event["amount"] = months;
		event["message"] = e.value("message").toObject().value("text").toString();
	} else if (type == "channel.subscription.gift") {
		int total = std::max(1, e.value("total").toInt());
		event["type"] = "gift_sub";
		event["tier"] = tierFrom(e);
		event["amount"] = total;
		event["count"] = total;
		event["anonymous"] = anonymous;
		if (!e.value("cumulative_total").isNull())
			event["cumulativeTotal"] = e.value("cumulative_total").toInt();
	} else if (type == "channel.cheer") {
		int bits = e.value("bits").toInt();
		event["type"] = "cheer";
		event["amount"] = bits;
		event["formattedAmount"] = QStringLiteral("%1 bits").arg(bits);
		event["message"] = e.value("message").toString();
		event["anonymous"] = anonymous;
	} else if (type == "channel.raid") {
		user = userFrom(e, "from_broadcaster_user");
		int viewers = e.value("viewers").toInt();
		event["type"] = "raid";
		event["amount"] = viewers;
		event["count"] = viewers;
	} else if (type == "channel.channel_points_custom_reward_redemption.add") {
		QJsonObject reward = e.value("reward").toObject();
		int cost = reward.value("cost").toInt();
		event["type"] = "redemption";
		event["reward"] = reward.value("title").toString();
		event["amount"] = cost;
		event["formattedAmount"] = QStringLiteral("%1 points").arg(cost);
		event["message"] = e.value("user_input").toString();
	} else if (type == "channel.hype_train.begin") {
		int level = std::max(1, e.value("level").toInt(1));
		event["type"] = "hype_train";
		event["amount"] = level;
		event["level"] = level;
		QJsonObject last = e.value("last_contribution").toObject();
		user = userFrom(last);
	} else {
		return;
	}

	if (anonymous) {
		user["displayName"] = "Anonymous";
		user["id"] = "";
	} else {
		user["avatar"] = TwitchCache::instance().avatar(options.creds, user.value("id").toString());
	}
	event["user"] = user;
	EventBus::instance().publish(makeEventItem(event));
}

} // namespace sf::twitch
