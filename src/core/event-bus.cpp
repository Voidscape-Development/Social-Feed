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

#include "core/event-bus.hpp"

#include <algorithm>

namespace sf {

EventBus &EventBus::instance()
{
	static EventBus bus;
	return bus;
}

int EventBus::subscribe(Handler handler)
{
	std::lock_guard lock(dispatchMutex);
	int token = nextToken++;
	handlers.emplace(token, std::move(handler));
	return token;
}

void EventBus::unsubscribe(int token)
{
	std::lock_guard lock(dispatchMutex);
	handlers.erase(token);
}

void EventBus::dispatch(const FeedItem &item)
{
	std::lock_guard lock(dispatchMutex);
	for (auto &[token, handler] : handlers)
		handler(item);
}

void EventBus::publish(const FeedItem &item)
{
	if (item.kind == FeedKind::Event && !item.payload.value("test").toBool()) {
		std::lock_guard lock(historyMutex);
		events.push_back(item);
		while (events.size() > kMaxHistory)
			events.pop_front();
	}

	dispatch(item);

	if (item.kind == FeedKind::Event)
		emit historyChanged();
	emit itemPublished(kindName(item.kind), item.platform, summarizeItem(item));
}

bool EventBus::replay(const QString &id)
{
	FeedItem copy;
	{
		std::lock_guard lock(historyMutex);
		auto it = std::find_if(events.begin(), events.end(), [&](const FeedItem &e) { return e.id == id; });
		if (it == events.end())
			return false;
		copy = *it;
	}
	copy.payload["replay"] = true;
	/* A fresh id so overlays don't discard it as a duplicate. */
	copy.id = makeId();
	copy.payload["id"] = copy.id;
	dispatch(copy);
	return true;
}

std::vector<FeedItem> EventBus::history() const
{
	std::lock_guard lock(historyMutex);
	return {events.begin(), events.end()};
}

void EventBus::clearHistory()
{
	{
		std::lock_guard lock(historyMutex);
		events.clear();
	}
	emit historyChanged();
}

QString summarizeItem(const FeedItem &item)
{
	const QJsonObject &p = item.payload;
	QString name = p.value("user").toObject().value("displayName").toString();
	if (item.kind == FeedKind::Chat)
		return QStringLiteral("%1: %2").arg(name, p.value("text").toString());
	if (item.kind == FeedKind::Moderation)
		return QStringLiteral("%1 (%2)").arg(p.value("action").toString(), p.value("target").toString());

	QString type = p.value("type").toString();
	QString text = QStringLiteral("%1 - %2").arg(eventTypeLabel(type), name);
	QString amount = p.value("formattedAmount").toString();
	if (!amount.isEmpty())
		text += QStringLiteral(" (%1)").arg(amount);
	return text;
}

} // namespace sf
