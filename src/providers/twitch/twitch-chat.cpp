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

#include "providers/twitch/twitch-chat.hpp"

#include "core/event-bus.hpp"
#include "core/feed-types.hpp"

#include <plugin-support.h>
#include <obs-module.h>

#include <QJsonArray>
#include <QRandomGenerator>

#include <algorithm>

namespace sf::twitch {

namespace {

QString unescapeTag(const QString &value)
{
	QString out;
	out.reserve(value.size());
	for (int i = 0; i < value.size(); i++) {
		QChar c = value[i];
		if (c != '\\' || i + 1 >= value.size()) {
			if (c != '\\')
				out += c;
			continue;
		}
		QChar next = value[++i];
		if (next == ':')
			out += ';';
		else if (next == 's')
			out += ' ';
		else if (next == 'r')
			out += '\r';
		else if (next == 'n')
			out += '\n';
		else
			out += next;
	}
	return out;
}

QString tierLabel(const QString &plan)
{
	if (plan.compare("Prime", Qt::CaseInsensitive) == 0)
		return QStringLiteral("prime");
	return plan.isEmpty() ? QStringLiteral("1000") : plan;
}

/* Splits text into text/emote fragments using the IRC "emotes" tag, whose ranges are in
 * Unicode code points (not UTF-16 units). */
QJsonArray buildFragments(const QString &text, const QString &emotesTag)
{
	struct Range {
		int start;
		int end;
		QString id;
	};
	std::vector<Range> ranges;
	for (const QString &emote : emotesTag.split('/', Qt::SkipEmptyParts)) {
		QString id = emote.section(':', 0, 0);
		for (const QString &pos : emote.section(':', 1).split(',', Qt::SkipEmptyParts)) {
			bool okStart = false, okEnd = false;
			int start = pos.section('-', 0, 0).toInt(&okStart);
			int end = pos.section('-', 1, 1).toInt(&okEnd);
			if (okStart && okEnd && end >= start)
				ranges.push_back({start, end, id});
		}
	}
	std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.start < b.start; });

	QList<uint> cps = text.toUcs4();
	auto slice = [&](int from, int to) {
		return QString::fromUcs4(reinterpret_cast<const char32_t *>(cps.constData()) + from, to - from);
	};

	QJsonArray fragments;
	int cursor = 0;
	for (const Range &r : ranges) {
		if (r.start < cursor || r.end >= cps.size())
			continue;
		if (r.start > cursor)
			fragments.append(textFragment(slice(cursor, r.start)));
		QString name = slice(r.start, r.end + 1);
		fragments.append(emoteFragment(
			name, QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/2.0").arg(r.id),
			r.id));
		cursor = r.end + 1;
	}
	if (cursor < cps.size())
		fragments.append(textFragment(slice(cursor, cps.size())));
	return fragments;
}

} // namespace

IrcMessage parseIrcLine(const QString &rawLine)
{
	IrcMessage msg;
	QString line = rawLine;
	int pos = 0;

	if (line.startsWith('@')) {
		int space = line.indexOf(' ');
		if (space < 0)
			return msg;
		for (const QString &pair : line.mid(1, space - 1).split(';')) {
			int eq = pair.indexOf('=');
			if (eq < 0)
				msg.tags.insert(pair, QString());
			else
				msg.tags.insert(pair.left(eq), unescapeTag(pair.mid(eq + 1)));
		}
		pos = space + 1;
	}

	if (pos < line.size() && line[pos] == ':') {
		int space = line.indexOf(' ', pos);
		if (space < 0)
			return msg;
		msg.prefix = line.mid(pos + 1, space - pos - 1);
		pos = space + 1;
	}

	int trailingStart = line.indexOf(" :", pos);
	QString middle = trailingStart >= 0 ? line.mid(pos, trailingStart - pos) : line.mid(pos);
	QStringList parts = middle.split(' ', Qt::SkipEmptyParts);
	if (!parts.isEmpty())
		msg.command = parts.takeFirst();
	msg.params = parts;
	if (trailingStart >= 0)
		msg.params.append(line.mid(trailingStart + 2));
	return msg;
}

QString TwitchChat::lastError() const
{
	std::lock_guard lock(mutex);
	return error;
}

void TwitchChat::connect(const Options &opts)
{
	options = opts;
	dropped = false;
	registered = false;
	{
		std::lock_guard lock(mutex);
		joined.clear();
		error.clear();
	}

	net::WebSocketClient::Handlers handlers;
	handlers.onOpen = [this]() {
		sendRaw("CAP REQ :twitch.tv/tags twitch.tv/commands");
		if (!options.login.isEmpty() && !options.creds.accessToken.isEmpty()) {
			sendRaw("PASS oauth:" + options.creds.accessToken);
			sendRaw("NICK " + options.login.toLower());
		} else {
			sendRaw(QStringLiteral("NICK justinfan%1")
					.arg(10000 + QRandomGenerator::global()->bounded(80000)));
		}
	};
	handlers.onMessage = [this](const std::string &data) {
		for (const QString &line : QString::fromStdString(data).split("\r\n", Qt::SkipEmptyParts))
			handleLine(line);
	};
	handlers.onClose = [this](const std::string &reason) {
		{
			std::lock_guard lock(mutex);
			error = QString::fromStdString(reason);
			joined.clear();
		}
		registered = false;
		dropped = true;
	};
	socket.open(options.url, std::move(handlers));
}

void TwitchChat::disconnect()
{
	socket.close();
	registered = false;
}

void TwitchChat::setChannels(const QStringList &channels)
{
	{
		std::lock_guard lock(mutex);
		wanted = channels;
	}
	syncJoins();
}

void TwitchChat::setEventSubChannels(const QSet<QString> &channels)
{
	std::lock_guard lock(mutex);
	eventSubChannels = channels;
}

void TwitchChat::sendRaw(const QString &line)
{
	socket.send(line.toStdString());
}

void TwitchChat::syncJoins()
{
	if (!registered)
		return;

	QStringList toJoin, toPart;
	{
		std::lock_guard lock(mutex);
		for (const QString &channel : wanted) {
			if (!joined.contains(channel))
				toJoin.append(channel);
		}
		for (const QString &channel : joined) {
			if (!wanted.contains(channel))
				toPart.append(channel);
		}
		for (const QString &channel : toJoin)
			joined.insert(channel);
		for (const QString &channel : toPart)
			joined.remove(channel);
	}

	/* Twitch limits JOIN rate; small batches are fine for overlay use. */
	for (int i = 0; i < toJoin.size(); i += 10) {
		QStringList batch;
		for (const QString &c : toJoin.mid(i, 10))
			batch.append("#" + c);
		sendRaw("JOIN " + batch.join(','));
	}
	for (const QString &channel : toPart)
		sendRaw("PART #" + channel);
}

void TwitchChat::handleLine(const QString &line)
{
	IrcMessage msg = parseIrcLine(line);
	const QString &cmd = msg.command;

	if (cmd == "PING") {
		sendRaw("PONG :" + msg.trailing());
	} else if (cmd == "001") {
		registered = true;
		syncJoins();
	} else if (cmd == "PRIVMSG") {
		handlePrivmsg(msg);
	} else if (cmd == "USERNOTICE") {
		handleUserNotice(msg);
	} else if (cmd == "ROOMSTATE") {
		QString channel = msg.params.value(0).mid(1).toLower();
		QString roomId = msg.tag("room-id");
		if (!roomId.isEmpty()) {
			{
				std::lock_guard lock(mutex);
				roomIds.insert(channel, roomId);
			}
			if (roomCallback)
				roomCallback(channel, roomId);
		}
	} else if (cmd == "CLEARMSG") {
		QString channel = msg.params.value(0).mid(1).toLower();
		EventBus::instance().publish(
			makeModerationItem("twitch", channel, "delete_message", msg.tag("target-msg-id")));
	} else if (cmd == "CLEARCHAT") {
		QString channel = msg.params.value(0).mid(1).toLower();
		QString userId = msg.tag("target-user-id");
		if (userId.isEmpty())
			EventBus::instance().publish(makeModerationItem("twitch", channel, "clear_chat", QString()));
		else
			EventBus::instance().publish(makeModerationItem("twitch", channel, "clear_user", userId));
	} else if (cmd == "RECONNECT") {
		/* Server asks us to reconnect; the provider supervisor will see needsReconnect(). */
		dropped = true;
		socket.close();
	} else if (cmd == "NOTICE" && msg.trailing().contains("authentication failed", Qt::CaseInsensitive)) {
		{
			std::lock_guard lock(mutex);
			error = QStringLiteral("Twitch chat login failed");
		}
		obs_log(LOG_WARNING, "Twitch chat login failed; token may be expired");
		dropped = true;
		socket.close();
	}
}

QJsonObject TwitchChat::buildUser(const IrcMessage &msg, const QString &roomId) const
{
	QJsonArray badges;
	QJsonArray roles;
	QSet<QString> roleSet;

	for (const QString &badge : msg.tag("badges").split(',', Qt::SkipEmptyParts)) {
		QString set = badge.section('/', 0, 0);
		QString version = badge.section('/', 1, 1);
		QJsonObject entry{{"id", set}, {"version", version}};
		TwitchCache::Badge info;
		if (TwitchCache::instance().lookupBadge(roomId, set, version, info)) {
			entry["url"] = info.url;
			entry["title"] = info.title;
		}
		badges.append(entry);

		if (set == "broadcaster")
			roleSet.insert("broadcaster");
		else if (set == "moderator")
			roleSet.insert("moderator");
		else if (set == "vip")
			roleSet.insert("vip");
		else if (set == "subscriber" || set == "founder")
			roleSet.insert("subscriber");
	}
	if (msg.tag("mod") == "1")
		roleSet.insert("moderator");
	if (msg.tag("vip") == "1")
		roleSet.insert("vip");
	if (msg.tag("subscriber") == "1")
		roleSet.insert("subscriber");
	for (const char *role : {"broadcaster", "moderator", "vip", "subscriber"}) {
		if (roleSet.contains(role))
			roles.append(role);
	}

	QString login = msg.tag("login");
	if (login.isEmpty())
		login = msg.nick();
	QString displayName = msg.tag("display-name");
	if (displayName.isEmpty())
		displayName = login;

	return QJsonObject{{"id", msg.tag("user-id")},  {"login", login},   {"displayName", displayName},
			   {"color", msg.tag("color")}, {"badges", badges}, {"roles", roles}};
}

void TwitchChat::handlePrivmsg(const IrcMessage &msg)
{
	QString channel = msg.params.value(0).mid(1).toLower();
	QString text = msg.trailing();
	bool isAction = false;
	if (text.startsWith("\x01"
			    "ACTION ") &&
	    text.endsWith('\x01')) {
		text = text.mid(8, text.size() - 9);
		isAction = true;
	}

	QString roomId = msg.tag("room-id");
	QJsonObject user = buildUser(msg, roomId);
	int bits = msg.tag("bits").toInt();

	QJsonObject payload{{"id", msg.tag("id")},
			    {"platform", "twitch"},
			    {"channel", channel},
			    {"channelId", roomId},
			    {"user", user},
			    {"text", text},
			    {"fragments", buildFragments(text, msg.tag("emotes"))},
			    {"isAction", isAction},
			    {"firstMessage", msg.tag("first-msg") == "1"},
			    {"highlighted", msg.tag("msg-id") == "highlighted-message"},
			    {"timestamp", msg.tag("tmi-sent-ts").toLongLong()}};
	if (bits > 0)
		payload["bits"] = bits;
	if (!msg.tag("reply-parent-msg-id").isEmpty()) {
		payload["reply"] = QJsonObject{{"id", msg.tag("reply-parent-msg-id")},
					       {"user", msg.tag("reply-parent-display-name")},
					       {"text", msg.tag("reply-parent-msg-body")}};
	}
	EventBus::instance().publish(makeChatItem(payload));

	bool coveredByEventSub;
	{
		std::lock_guard lock(mutex);
		coveredByEventSub = eventSubChannels.contains(channel);
	}
	if (bits > 0 && !coveredByEventSub) {
		QJsonObject event{{"type", "cheer"},    {"platform", "twitch"},
				  {"channel", channel}, {"user", user},
				  {"amount", bits},     {"formattedAmount", QStringLiteral("%1 bits").arg(bits)},
				  {"message", text}};
		EventBus::instance().publish(makeEventItem(event));
	}
}

void TwitchChat::handleUserNotice(const IrcMessage &msg)
{
	QString channel = msg.params.value(0).mid(1).toLower();
	{
		std::lock_guard lock(mutex);
		if (eventSubChannels.contains(channel))
			return;
	}

	QString kind = msg.tag("msg-id");
	QJsonObject user = buildUser(msg, msg.tag("room-id"));
	user["avatar"] = TwitchCache::instance().avatar(options.creds, user.value("id").toString());

	QJsonObject event{{"platform", "twitch"},
			  {"channel", channel},
			  {"user", user},
			  {"message", msg.params.size() > 1 ? msg.trailing() : QString()}};

	if (kind == "sub" || kind == "resub") {
		int months = std::max(1, msg.tag("msg-param-cumulative-months").toInt());
		event["type"] = "subscription";
		event["tier"] = tierLabel(msg.tag("msg-param-sub-plan"));
		event["months"] = months;
		event["amount"] = months;
	} else if (kind == "subgift" || kind == "anonsubgift") {
		/* Individual gifts belonging to a mystery gift are summarized by submysterygift. */
		if (!msg.tag("msg-param-community-gift-id").isEmpty())
			return;
		event["type"] = "gift_sub";
		event["tier"] = tierLabel(msg.tag("msg-param-sub-plan"));
		event["amount"] = 1;
		event["count"] = 1;
		event["recipient"] = msg.tag("msg-param-recipient-display-name");
		event["anonymous"] = kind == "anonsubgift";
	} else if (kind == "submysterygift" || kind == "anonsubmysterygift") {
		int count = std::max(1, msg.tag("msg-param-mass-gift-count").toInt());
		event["type"] = "gift_sub";
		event["tier"] = tierLabel(msg.tag("msg-param-sub-plan"));
		event["amount"] = count;
		event["count"] = count;
		event["anonymous"] = kind == "anonsubmysterygift";
	} else if (kind == "raid") {
		int viewers = msg.tag("msg-param-viewerCount").toInt();
		user["displayName"] = msg.tag("msg-param-displayName").isEmpty()
					      ? user.value("displayName")
					      : QJsonValue(msg.tag("msg-param-displayName"));
		event["user"] = user;
		event["type"] = "raid";
		event["amount"] = viewers;
		event["count"] = viewers;
	} else {
		return;
	}

	EventBus::instance().publish(makeEventItem(event));
}

} // namespace sf::twitch
