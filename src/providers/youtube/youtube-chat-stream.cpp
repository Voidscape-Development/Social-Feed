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

#include "providers/youtube/youtube-chat-stream.hpp"

#include "core/event-bus.hpp"
#include "core/feed-types.hpp"
#include "net/http-client.hpp"
#include "providers/youtube/youtube-api.hpp"
#include "providers/youtube/youtube-chat-parser.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace sf::youtube {

namespace {
/* Reconnect if a stream stays completely silent this long. Measured stream lifetimes will tell
 * whether YouTube sends keepalives; until then this only guards against dead connections. */
constexpr long kIdleTimeoutSeconds = 300;
} // namespace

/* ---- JsonStreamSplitter ---- */

void JsonStreamSplitter::setContentType(const std::string &contentType)
{
	sse = contentType.find("event-stream") != std::string::npos;
}

void JsonStreamSplitter::feed(const char *data, size_t size, std::vector<std::string> &out)
{
	for (size_t i = 0; i < size; i++) {
		if (sse)
			feedSse(data[i], out);
		else
			feedJson(data[i], out);
	}
}

void JsonStreamSplitter::feedJson(char c, std::vector<std::string> &out)
{
	if (depth == 0) {
		/* Between objects: "[", ",", "]" and whitespace are framing. */
		if (c != '{')
			return;
		current.clear();
	}

	current += c;
	if (inString) {
		if (escaped)
			escaped = false;
		else if (c == '\\')
			escaped = true;
		else if (c == '"')
			inString = false;
		return;
	}

	if (c == '"') {
		inString = true;
	} else if (c == '{' || c == '[') {
		depth++;
	} else if (c == '}' || c == ']') {
		depth--;
		if (depth == 0) {
			out.push_back(current);
			current.clear();
		}
	}
}

void JsonStreamSplitter::feedSse(char c, std::vector<std::string> &out)
{
	if (c == '\r')
		return;
	if (c != '\n') {
		line += c;
		return;
	}
	if (line.empty()) {
		/* Blank line ends the event. */
		if (!eventData.empty())
			out.push_back(eventData);
		eventData.clear();
	} else if (line.rfind("data:", 0) == 0) {
		std::string value = line.substr(5);
		if (!value.empty() && value[0] == ' ')
			value.erase(0, 1);
		if (!eventData.empty())
			eventData += '\n';
		eventData += value;
	}
	line.clear();
}

/* ---- StreamConnection ---- */

bool StreamConnection::endpointUnavailable() const
{
	/* A Google API error is JSON; anything else (HTML 404 page, 405, 501) means the method is not
	 * served. 400 means our request shape is not accepted, which a retry won't fix either. */
	bool googleError = contentType.contains("json");
	return httpStatus == 400 || httpStatus == 405 || httpStatus == 501 || (httpStatus == 404 && !googleError);
}

static void readError(const QJsonObject &error, QString &reason, QString &message)
{
	QJsonArray errors = error.value("errors").toArray();
	reason = errors.isEmpty() ? QString() : errors.first().toObject().value("reason").toString();
	if (reason.isEmpty())
		reason = error.value("status").toString();
	message = error.value("message").toString();
}

void StreamConnection::run()
{
	startedMs = nowMs();
	if (!Quota::instance().tryConsume(assumedCost)) {
		endReason = QStringLiteral("budget");
		return;
	}

	QString url = QStringLiteral("%1/liveChat/messages/stream?liveChatId=%2&part=snippet&part=authorDetails")
			      .arg(apiBase(true), QString::fromStdString(net::urlEncode(liveChatId.toStdString())));
	if (!pageToken.isEmpty())
		url += "&pageToken=" + QString::fromStdString(net::urlEncode(pageToken.toStdString()));

	JsonStreamSplitter splitter;
	bool ended = false;
	std::vector<std::string> objects;

	net::StreamOptions options;
	options.shouldAbort = shouldAbort;
	options.idleTimeoutSeconds = kIdleTimeoutSeconds;
	options.onHeaders = [&](long, const std::string &type) {
		splitter.setContentType(type);
	};
	options.onData = [&](const char *data, size_t size) {
		objects.clear();
		splitter.feed(data, size, objects);

		for (const std::string &raw : objects) {
			QJsonObject object = QJsonDocument::fromJson(QByteArray::fromStdString(raw)).object();
			if (object.contains("error")) {
				/* The RPC failed after the stream started. */
				readError(object.value("error").toObject(), reason, error);
				return false;
			}

			responses++;
			QString next = object.value("nextPageToken").toString();
			if (!next.isEmpty())
				pageToken = next;

			ParseResult parsed = parseChatMessages(object.value("items").toArray(), channel);
			items += (int)object.value("items").toArray().size();
			/* A fresh connection starts with recent history; don't replay it. */
			if (primed) {
				for (const FeedItem &item : parsed.items)
					EventBus::instance().publish(item);
			}
			primed = true;

			if (parsed.chatEnded || object.contains("offlineAt")) {
				ended = true;
				return false;
			}
		}
		return true;
	};

	net::StreamResponse response = net::streamGet(
		url.toStdString(), {"Authorization: Bearer " + accessToken.toStdString(), "Accept: application/json"},
		options);

	httpStatus = response.status;
	contentType = QString::fromStdString(response.contentType);
	bytes = response.bytes;
	firstByteMs = response.firstByteMs;
	durationMs = nowMs() - startedMs;

	if (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300)) {
		QJsonObject body = QJsonDocument::fromJson(QByteArray::fromStdString(response.body)).object();
		readError(body.value("error").toObject(), reason, error);
		if (error.isEmpty())
			error = QStringLiteral("HTTP %1").arg(httpStatus);
		endReason = QStringLiteral("error");
	} else if (ended) {
		endReason = QStringLiteral("chat-ended");
	} else if (!reason.isEmpty()) {
		endReason = QStringLiteral("error");
	} else if (response.idleTimeout) {
		endReason = QStringLiteral("idle");
	} else if (response.aborted) {
		endReason = QStringLiteral("aborted");
	} else if (!response.error.empty()) {
		endReason = QStringLiteral("error");
		error = QString::fromStdString(response.error);
	} else {
		endReason = QStringLiteral("eof");
	}
}

QString StreamConnection::csvHeader()
{
	return QStringLiteral("started_utc,duration_s,http_status,content_type,first_byte_ms,bytes,responses,items,"
			      "end_reason,reason,error,assumed_cost,units_used_today_estimate");
}

QString StreamConnection::csvLine() const
{
	auto quote = [](QString value) {
		value.replace('"', "\"\"");
		return "\"" + value + "\"";
	};
	return QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12")
		.arg(QDateTime::fromMSecsSinceEpoch(startedMs).toUTC().toString(Qt::ISODate))
		.arg(durationMs / 1000.0, 0, 'f', 1)
		.arg(httpStatus)
		.arg(quote(contentType))
		.arg(firstByteMs)
		.arg((qulonglong)bytes)
		.arg(responses)
		.arg(items)
		.arg(endReason)
		.arg(quote(reason))
		.arg(quote(error))
		.arg(assumedCost);
}

QString streamLogPath()
{
	char *path = obs_module_config_path("youtube-stream-log.csv");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

void logStreamConnection(const StreamConnection &c, int unitsUsedToday)
{
	obs_log(LOG_INFO,
		"YouTube stream connection: %.1fs, HTTP %ld, type '%s', first byte %lld ms, %zu bytes, %d responses, "
		"%d items, end=%s reason='%s' error='%s'",
		c.durationMs / 1000.0, c.httpStatus, c.contentType.toUtf8().constData(), (long long)c.firstByteMs,
		c.bytes, c.responses, c.items, c.endReason.toUtf8().constData(), c.reason.toUtf8().constData(),
		c.error.toUtf8().constData());

	QString path = streamLogPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	bool isNew = !file.exists();
	if (!file.open(QIODevice::Append | QIODevice::Text))
		return;
	if (isNew)
		file.write((StreamConnection::csvHeader() + "\n").toUtf8());
	file.write((c.csvLine() + "," + QString::number(unitsUsedToday) + "\n").toUtf8());
}

} // namespace sf::youtube
