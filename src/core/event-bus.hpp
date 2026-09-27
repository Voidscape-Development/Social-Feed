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

#include "core/feed-types.hpp"

#include <QObject>

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace sf {

/* Central fan-out point. Providers publish from their own threads; overlay sources subscribe
 * and forward items to their browser. Events (not chat) are kept in a bounded history so they
 * can be replayed from the dock. */
class EventBus : public QObject {
	Q_OBJECT

public:
	using Handler = std::function<void(const FeedItem &)>;

	static EventBus &instance();

	int subscribe(Handler handler);
	void unsubscribe(int token);

	void publish(const FeedItem &item);
	/* Re-sends a stored event with payload.replay = true. */
	bool replay(const QString &id);

	std::vector<FeedItem> history() const;
	void clearHistory();

	static constexpr size_t kMaxHistory = 200;

signals:
	void historyChanged();
	/* Emitted for every published item; connect with Qt::QueuedConnection for UI use. */
	void itemPublished(const QString &kind, const QString &platform, const QString &summary);

private:
	EventBus() = default;
	void dispatch(const FeedItem &item);

	mutable std::mutex historyMutex;
	std::deque<FeedItem> events;

	/* dispatchMutex serializes delivery and guarantees that once unsubscribe() returns the
	 * handler is never called again. Handlers must not (un)subscribe. */
	std::mutex dispatchMutex;
	std::map<int, Handler> handlers;
	int nextToken = 1;
};

QString summarizeItem(const FeedItem &item);

} // namespace sf
