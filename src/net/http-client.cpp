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
