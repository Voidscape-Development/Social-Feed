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

#include "net/http-client.hpp"

#include <QJsonDocument>

#include <curl/curl.h>

#include <cctype>
#include <chrono>

namespace sf::net {

QJsonObject HttpResponse::json() const
{
	return QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
}

std::string HttpResponse::describe() const
{
	if (!error.empty())
		return error;
	return "HTTP " + std::to_string(status) + ": " + body.substr(0, 300);
}

static size_t writeCallback(char *data, size_t size, size_t count, void *userdata)
{
	auto *out = static_cast<std::string *>(userdata);
	out->append(data, size * count);
	return size * count;
}

HttpResponse request(const std::string &method, const std::string &url, const std::vector<std::string> &headers,
		     const std::string &body, long timeoutSeconds)
{
	HttpResponse response;
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.error = "curl_easy_init failed";
		return response;
	}

	char errorBuffer[CURL_ERROR_SIZE] = {};
	struct curl_slist *headerList = nullptr;
	for (const auto &header : headers)
		headerList = curl_slist_append(headerList, header.c_str());

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "SocialFeed-OBS/0.1");
	if (!body.empty() || method == "POST" || method == "PATCH" || method == "PUT") {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
	}

	CURLcode code = curl_easy_perform(curl);
	if (code != CURLE_OK)
		response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);

	curl_slist_free_all(headerList);
	curl_easy_cleanup(curl);
	return response;
}

namespace {

struct StreamState {
	CURL *curl = nullptr;
	const StreamOptions *options = nullptr;
	StreamResponse *response = nullptr;
	std::chrono::steady_clock::time_point start;
	std::chrono::steady_clock::time_point lastData;
	bool stoppedByCallback = false;
};

size_t streamWrite(char *data, size_t size, size_t count, void *userdata)
{
	auto *state = static_cast<StreamState *>(userdata);
	size_t bytes = size * count;
	auto now = std::chrono::steady_clock::now();
	state->lastData = now;

	StreamResponse &response = *state->response;
	if (response.bytes == 0) {
		curl_easy_getinfo(state->curl, CURLINFO_RESPONSE_CODE, &response.status);
		char *type = nullptr;
		curl_easy_getinfo(state->curl, CURLINFO_CONTENT_TYPE, &type);
		response.contentType = type ? type : "";
		response.firstByteMs =
			std::chrono::duration_cast<std::chrono::milliseconds>(now - state->start).count();
		if (state->options->onHeaders)
			state->options->onHeaders(response.status, response.contentType);
	}
	response.bytes += bytes;

	if (response.status < 200 || response.status >= 300) {
		if (response.body.size() < 64 * 1024)
			response.body.append(data, bytes);
		return bytes;
	}
	if (state->options->onData && !state->options->onData(data, bytes)) {
		state->stoppedByCallback = true;
		return 0; /* makes curl abort with CURLE_WRITE_ERROR */
	}
	return bytes;
}

int streamProgress(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	auto *state = static_cast<StreamState *>(userdata);
	if (state->options->shouldAbort && state->options->shouldAbort()) {
		state->response->aborted = true;
		return 1;
	}
	long idle = state->options->idleTimeoutSeconds;
	if (idle > 0 && std::chrono::steady_clock::now() - state->lastData > std::chrono::seconds(idle)) {
		state->response->idleTimeout = true;
		return 1;
	}
	return 0;
}

} // namespace

StreamResponse streamGet(const std::string &url, const std::vector<std::string> &headers, const StreamOptions &options)
{
	StreamResponse response;
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.error = "curl_easy_init failed";
		return response;
	}

	StreamState state;
	state.curl = curl;
	state.options = &options;
	state.response = &response;
	state.start = state.lastData = std::chrono::steady_clock::now();

	char errorBuffer[CURL_ERROR_SIZE] = {};
	struct curl_slist *headerList = nullptr;
	for (const auto &header : headers)
		headerList = curl_slist_append(headerList, header.c_str());

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, streamProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "SocialFeed-OBS/0.1");
	/* No compression: some servers buffer compressed streams until a block fills. */

	CURLcode code = curl_easy_perform(curl);
	if (response.status == 0)
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
	if (code != CURLE_OK && !response.aborted && !response.idleTimeout && !state.stoppedByCallback)
		response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
	if (state.stoppedByCallback)
		response.aborted = true;

	curl_slist_free_all(headerList);
	curl_easy_cleanup(curl);
	return response;
}

std::string urlEncode(const std::string &value)
{
	std::string out;
	static const char hex[] = "0123456789ABCDEF";
	for (unsigned char c : value) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields)
{
	std::string out;
	for (const auto &[key, value] : fields) {
		if (!out.empty())
			out += '&';
		out += urlEncode(key) + '=' + urlEncode(value);
	}
	return out;
}

void globalInit()
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

void globalCleanup()
{
	curl_global_cleanup();
}

} // namespace sf::net
