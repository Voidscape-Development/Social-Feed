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

#include <QJsonObject>

#include <functional>
#include <string>
#include <vector>

namespace sf::net {

/* Blocking HTTPS requests through libcurl. OBS ships libcurl with a working TLS backend on every
 * platform, which Qt's network module does not (no TLS plugin is bundled), so all platform
 * traffic goes through curl. Never call from the OBS UI or graphics threads. */
struct HttpResponse {
	long status = 0;
	std::string body;
	std::string error;

	bool ok() const { return error.empty() && status >= 200 && status < 300; }
	QJsonObject json() const;
	std::string describe() const;
};

HttpResponse request(const std::string &method, const std::string &url, const std::vector<std::string> &headers = {},
		     const std::string &body = {}, long timeoutSeconds = 15);

inline HttpResponse get(const std::string &url, const std::vector<std::string> &headers = {})
{
	return request("GET", url, headers);
}

/* Long-lived streaming GET. 2xx bodies are handed to onData as they arrive (return false to stop);
 * error bodies are collected into the response instead. The transfer ends when the server closes
 * it, shouldAbort() returns true, or no bytes arrive for idleTimeoutSeconds (0 = no limit). */
struct StreamOptions {
	/* Called once, before the first onData, with the response status and Content-Type. */
	std::function<void(long status, const std::string &contentType)> onHeaders;
	std::function<bool(const char *data, size_t size)> onData;
	std::function<bool()> shouldAbort;
	long idleTimeoutSeconds = 0;
};

struct StreamResponse : HttpResponse {
	std::string contentType;
	bool aborted = false;     /* shouldAbort() or onData stopped it */
	bool idleTimeout = false; /* no data for idleTimeoutSeconds */
	size_t bytes = 0;
	long long firstByteMs = -1; /* time to first body byte */
};

StreamResponse streamGet(const std::string &url, const std::vector<std::string> &headers, const StreamOptions &options);

std::string urlEncode(const std::string &value);
/* application/x-www-form-urlencoded body from key/value pairs. */
std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields);

void globalInit();
void globalCleanup();

} // namespace sf::net
