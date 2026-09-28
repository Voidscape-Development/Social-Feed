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

#include <QJsonArray>

#include <vector>

namespace sf::youtube {

struct ParseResult {
	std::vector<FeedItem> items;
	bool chatEnded = false;
};

/* Turns liveChatMessages.list items into feed items: chat messages, events (Super Chat,
 * Super Sticker, new members, milestones, gifted memberships) and moderation (deleted
 * messages, bans). `channel` is the owning channel's handle, used as the item channel. */
ParseResult parseChatMessages(const QJsonArray &items, const QString &channel);

} // namespace sf::youtube
