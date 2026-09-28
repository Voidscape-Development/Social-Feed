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

#include <QString>

#include <functional>
#include <string>
#include <vector>

namespace sf::youtube {

/* Splits a streamed HTTP body into complete JSON objects. Handles both framings Google uses for
 * server-streamed REST methods: a JSON array written incrementally ("[{…}\n,{…}\n]") and
 * Server-Sent Events ("data: {…}\n\n"). */
class JsonStreamSplitter {
public:
	void setContentType(const std::string &contentType);
	void feed(const char *data, size_t size, std::vector<std::string> &out);

private:
	void feedJson(char c, std::vector<std::string> &out);
	void feedSse(char c, std::vector<std::string> &out);

	bool sse = false;
	/* JSON mode */
	std::string current;
	int depth = 0;
	bool inString = false;
	bool escaped = false;
	/* SSE mode */
	std::string line;
	std::string eventData;
};

/* One connection to liveChatMessages.streamList (REST: GET liveChat/messages/stream).
 * Publishes feed items as they arrive and reports what happened, for reconnect decisions and
 * for the measurement log. */
struct StreamConnection {
	/* inputs */
	QString accessToken;
	QString liveChatId;
	QString channel;
	int assumedCost = 5;
	std::function<bool()> shouldAbort;

	/* in/out: resume position and backlog handling */
	QString pageToken;
	bool primed = false;

	/* results */
	long httpStatus = 0;
	QString contentType;
	QString reason;    /* Google error reason or gRPC-style status */
	QString error;     /* human-readable */
	QString endReason; /* eof | aborted | idle | chat-ended | error | budget */
	qint64 startedMs = 0;
	qint64 durationMs = 0;
	qint64 firstByteMs = -1;
	size_t bytes = 0;
	int responses = 0;
	int items = 0;

	bool unauthorized() const { return httpStatus == 401 || reason == "UNAUTHENTICATED"; }
	bool quotaExceeded() const
	{
		return endReason == "budget" || reason == "quotaExceeded" || reason == "dailyLimitExceeded" ||
		       reason == "RESOURCE_EXHAUSTED";
	}
	bool chatEnded() const
	{
		return endReason == "chat-ended" || reason == "liveChatEnded" || reason == "liveChatNotFound" ||
		       reason == "liveChatDisabled" || reason == "FAILED_PRECONDITION" || reason == "NOT_FOUND" ||
		       reason == "PERMISSION_DENIED";
	}
	/* The endpoint itself looks unusable (not a chat-level error). */
	bool endpointUnavailable() const;

	void run();
	/* One CSV line for youtube-stream-log.csv. */
	QString csvLine() const;
	static QString csvHeader();
};

/* Appends a connection record to <plugin config>/youtube-stream-log.csv and the OBS log. */
void logStreamConnection(const StreamConnection &connection, int unitsUsedToday);
QString streamLogPath();

} // namespace sf::youtube
