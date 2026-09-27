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

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sf::net {

/* Minimal RFC 6455 client. TLS and TCP are handled by libcurl in CONNECT_ONLY mode (available in
 * every curl OBS ships, unlike curl's own WebSocket API), framing is done here. Each open()
 * runs one connection on a dedicated thread; reconnect policy belongs to the owner.
 *
 * All handlers run on the connection thread. onClose is always called exactly once per open(),
 * including when the connection could not be established. */
class WebSocketClient {
public:
	struct Handlers {
		std::function<void()> onOpen;
		std::function<void(const std::string &)> onMessage;
		std::function<void(const std::string &reason)> onClose;
	};

	WebSocketClient() = default;
	~WebSocketClient();

	WebSocketClient(const WebSocketClient &) = delete;
	WebSocketClient &operator=(const WebSocketClient &) = delete;

	/* url: ws:// or wss://. Any previous connection is closed first. Must not be called from a
	 * handler; reconnect from the owner's own thread instead. */
	void open(const std::string &url, Handlers handlers, std::vector<std::string> extraHeaders = {});
	/* Closes and joins. Safe to call from a handler (it then only requests the stop). */
	void close();
	/* Queues a text frame; returns false when not connected. */
	bool send(const std::string &text);
	bool connected() const { return isConnected; }

private:
	void run(std::string url, std::vector<std::string> extraHeaders, Handlers handlers);

	std::thread worker;
	std::atomic<bool> stopRequested{false};
	std::atomic<bool> isConnected{false};

	std::mutex queueMutex;
	std::deque<std::string> outgoing; /* already-framed bytes */
};

} // namespace sf::net
