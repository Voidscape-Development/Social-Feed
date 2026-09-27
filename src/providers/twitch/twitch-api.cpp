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

#include "providers/twitch/twitch-api.hpp"

#include <QJsonArray>
#include <QJsonDocument>

namespace sf::twitch {

net::HttpResponse helix(const Credentials &creds, const std::string &method, const std::string &path,
			const QJsonObject &body)
{
	std::vector<std::string> headers = {"Client-Id: " + creds.clientId.toStdString(),
					    "Authorization: Bearer " + creds.accessToken.toStdString()};
	std::string payload;
	if (!body.isEmpty()) {
		headers.push_back("Content-Type: application/json");
		payload = QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
	}
	return net::request(method, "https://api.twitch.tv/helix" + path, headers, payload);
}

TwitchCache &TwitchCache::instance()
{
	static TwitchCache cache;
	return cache;
}

bool TwitchCache::loadBadgeSet(const Credentials &creds, const QString &key, const std::string &path)
{
	auto response = helix(creds, "GET", path);
	if (!response.ok())
		return false;

	QHash<QString, Badge> loaded;
	for (const auto &setValue : response.json().value("data").toArray()) {
		QJsonObject set = setValue.toObject();
		QString setId = set.value("set_id").toString();
		for (const auto &versionValue : set.value("versions").toArray()) {
			QJsonObject version = versionValue.toObject();
			loaded.insert(setId + "/" + version.value("id").toString(),
				      Badge{version.value("image_url_2x").toString(),
					    version.value("title").toString()});
		}
	}

	std::lock_guard lock(mutex);
	badges.insert(key, loaded);
	return true;
}

void TwitchCache::ensureBadges(const Credentials &creds, const QString &roomId)
{
	if (!creds.valid())
		return;

	bool needGlobal, needRoom;
	{
		std::lock_guard lock(mutex);
		needGlobal = !attempted.contains("global");
		needRoom = !roomId.isEmpty() && !attempted.contains(roomId);
		if (needGlobal)
			attempted.insert("global");
		if (needRoom)
			attempted.insert(roomId);
	}

	if (needGlobal && !loadBadgeSet(creds, "global", "/chat/badges/global")) {
		std::lock_guard lock(mutex);
		attempted.remove("global");
	}
	if (needRoom && !loadBadgeSet(creds, roomId, "/chat/badges?broadcaster_id=" + roomId.toStdString())) {
		std::lock_guard lock(mutex);
		attempted.remove(roomId);
	}
}

bool TwitchCache::lookupBadge(const QString &roomId, const QString &set, const QString &version, Badge &out) const
{
	QString key = set + "/" + version;
	std::lock_guard lock(mutex);
	auto room = badges.find(roomId);
	if (room != badges.end() && room->contains(key)) {
		out = room->value(key);
		return true;
	}
	auto global = badges.find("global");
	if (global != badges.end() && global->contains(key)) {
		out = global->value(key);
		return true;
	}
	return false;
}

QString TwitchCache::avatar(const Credentials &creds, const QString &userId)
{
	if (userId.isEmpty() || !creds.valid())
		return {};
	{
		std::lock_guard lock(mutex);
		auto it = avatars.find(userId);
		if (it != avatars.end())
			return it.value();
	}

	auto response = helix(creds, "GET", "/users?id=" + userId.toStdString());
	QString url;
	if (response.ok()) {
		QJsonArray data = response.json().value("data").toArray();
		if (!data.isEmpty())
			url = data.first().toObject().value("profile_image_url").toString();
	}

	std::lock_guard lock(mutex);
	if (avatars.size() > 2000)
		avatars.clear();
	avatars.insert(userId, url);
	return url;
}

void TwitchCache::clear()
{
	std::lock_guard lock(mutex);
	badges.clear();
	attempted.clear();
	avatars.clear();
}

} // namespace sf::twitch
