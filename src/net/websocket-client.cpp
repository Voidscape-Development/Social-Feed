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

#include "net/websocket-client.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>

#include <curl/curl.h>

#ifndef _WIN32
#include <sys/select.h>
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace sf::net {

namespace {

enum Opcode : uint8_t {
	OpContinuation = 0x0,
	OpText = 0x1,
	OpBinary = 0x2,
	OpClose = 0x8,
	OpPing = 0x9,
	OpPong = 0xA,
};

struct ParsedUrl {
	bool secure = true;
	std::string host;
	int port = 443;
	std::string path = "/";
	bool valid = false;
};

ParsedUrl parseUrl(const std::string &url)
{
	ParsedUrl out;
	std::string rest;
	if (url.rfind("wss://", 0) == 0) {
		out.secure = true;
		out.port = 443;
		rest = url.substr(6);
	} else if (url.rfind("ws://", 0) == 0) {
		out.secure = false;
		out.port = 80;
		rest = url.substr(5);
	} else {
		return out;
	}

	size_t slash = rest.find('/');
	std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
	if (slash != std::string::npos)
		out.path = rest.substr(slash);

	size_t colon = authority.rfind(':');
	if (colon != std::string::npos && authority.find(']') == std::string::npos) {
		out.host = authority.substr(0, colon);
		out.port = std::atoi(authority.substr(colon + 1).c_str());
	} else {
		out.host = authority;
	}
	out.valid = !out.host.empty() && out.port > 0;
	return out;
}

std::string buildFrame(Opcode opcode, const std::string &payload)
{
	std::string frame;
	frame.reserve(payload.size() + 14);
	frame += (char)(0x80 | opcode);

	size_t len = payload.size();
	if (len < 126) {
		frame += (char)(0x80 | len);
	} else if (len <= 0xFFFF) {
		frame += (char)(0x80 | 126);
		frame += (char)((len >> 8) & 0xFF);
		frame += (char)(len & 0xFF);
	} else {
		frame += (char)(0x80 | 127);
		for (int i = 7; i >= 0; i--)
			frame += (char)(((uint64_t)len >> (8 * i)) & 0xFF);
	}

	uint32_t maskValue = QRandomGenerator::global()->generate();
	char mask[4];
	memcpy(mask, &maskValue, 4);
	frame.append(mask, 4);

	for (size_t i = 0; i < len; i++)
		frame += (char)(payload[i] ^ mask[i % 4]);
	return frame;
}

int waitSocket(curl_socket_t fd, bool forRead, int timeoutMs)
{
	struct timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;

	fd_set readSet, writeSet;
	FD_ZERO(&readSet);
	FD_ZERO(&writeSet);
	FD_SET(fd, forRead ? &readSet : &writeSet);
	return select((int)fd + 1, forRead ? &readSet : nullptr, forRead ? nullptr : &writeSet, nullptr, &tv);
}

class Connection {
public:
	explicit Connection(std::atomic<bool> &stop) : stop(stop) {}
	~Connection()
	{
		if (curl)
			curl_easy_cleanup(curl);
	}

	std::string error;

	bool connect(const ParsedUrl &url)
	{
		curl = curl_easy_init();
		if (!curl) {
			error = "curl_easy_init failed";
			return false;
		}

		std::string curlUrl = std::string(url.secure ? "https://" : "http://") + url.host + ":" +
				      std::to_string(url.port) + "/";
		curl_easy_setopt(curl, CURLOPT_URL, curlUrl.c_str());
		curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

		CURLcode code = curl_easy_perform(curl);
		if (code != CURLE_OK) {
			error = curl_easy_strerror(code);
			return false;
		}
		code = curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket);
		if (code != CURLE_OK || socket == CURL_SOCKET_BAD) {
			error = "could not obtain socket";
			return false;
		}
		return true;
	}

	/* force: keep sending after a stop was requested (used for the final close frame). */
	bool sendAll(const std::string &data, bool force = false)
	{
		size_t offset = 0;
		while (offset < data.size()) {
			if (stop && !force)
				return false;
			size_t sent = 0;
			CURLcode code = curl_easy_send(curl, data.data() + offset, data.size() - offset, &sent);
			if (code == CURLE_AGAIN) {
				waitSocket(socket, false, 100);
				continue;
			}
			if (code != CURLE_OK) {
				error = curl_easy_strerror(code);
				return false;
			}
			offset += sent;
		}
		return true;
	}

	/* Appends whatever is available to buffer. Returns false on error/EOF. */
	bool receive(std::string &buffer, int waitMs)
	{
		char chunk[16384];
		bool gotData = false;
		for (;;) {
			size_t received = 0;
			CURLcode code = curl_easy_recv(curl, chunk, sizeof(chunk), &received);
			if (code == CURLE_AGAIN)
				break;
			if (code != CURLE_OK) {
				error = curl_easy_strerror(code);
				return false;
			}
			if (received == 0) {
				error = "connection closed by peer";
				return false;
			}
			buffer.append(chunk, received);
			gotData = true;
		}
		/* TLS may already hold decrypted bytes, so we only block on the socket after a
		 * recv attempt reported CURLE_AGAIN. */
		if (!gotData && waitMs > 0)
			waitSocket(socket, true, waitMs);
		return true;
	}

private:
	std::atomic<bool> &stop;
	CURL *curl = nullptr;
	curl_socket_t socket = CURL_SOCKET_BAD;
};

} // namespace

WebSocketClient::~WebSocketClient()
{
	close();
}

void WebSocketClient::open(const std::string &url, Handlers handlers, std::vector<std::string> extraHeaders)
{
	/* Reopening from inside a handler would replace the thread that is running us. */
	if (worker.joinable() && worker.get_id() == std::this_thread::get_id())
		return;
	close();
	stopRequested = false;
	{
		std::lock_guard lock(queueMutex);
		outgoing.clear();
	}
	worker = std::thread(&WebSocketClient::run, this, url, std::move(extraHeaders), std::move(handlers));
}

void WebSocketClient::close()
{
	stopRequested = true;
	if (!worker.joinable())
		return;
	if (worker.get_id() == std::this_thread::get_id()) {
		/* Called from a handler: the loop exits on its own; the thread is joined by the
		 * next open()/close() from another thread. */
		return;
	}
	worker.join();
}

bool WebSocketClient::send(const std::string &text)
{
	if (!isConnected)
		return false;
	std::lock_guard lock(queueMutex);
	outgoing.push_back(buildFrame(OpText, text));
	return true;
}

void WebSocketClient::run(std::string urlString, std::vector<std::string> extraHeaders, Handlers handlers)
{
	auto finish = [&](const std::string &reason) {
		isConnected = false;
		if (handlers.onClose)
			handlers.onClose(reason);
	};

	ParsedUrl url = parseUrl(urlString);
	if (!url.valid) {
		finish("invalid url: " + urlString);
		return;
	}

	Connection conn(stopRequested);
	if (!conn.connect(url)) {
		finish(conn.error);
		return;
	}

	/* Handshake */
	QByteArray keyBytes(16, 0);
	for (int i = 0; i < 16; i++)
		keyBytes[i] = (char)QRandomGenerator::global()->bounded(256);
	std::string key = keyBytes.toBase64().toStdString();

	bool defaultPort = (url.secure && url.port == 443) || (!url.secure && url.port == 80);
	std::string request = "GET " + url.path + " HTTP/1.1\r\n";
	request += "Host: " + url.host + (defaultPort ? "" : ":" + std::to_string(url.port)) + "\r\n";
	request += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
	request += "Sec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n";
	request += "User-Agent: SocialFeed-OBS/0.1\r\n";
	for (const auto &header : extraHeaders)
		request += header + "\r\n";
	request += "\r\n";

	if (!conn.sendAll(request)) {
		finish("handshake send failed: " + conn.error);
		return;
	}

	std::string buffer;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	size_t headerEnd = std::string::npos;
	while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
		if (stopRequested || std::chrono::steady_clock::now() > deadline) {
			finish("handshake timed out");
			return;
		}
		if (!conn.receive(buffer, 100)) {
			finish("handshake failed: " + conn.error);
			return;
		}
	}

	std::string responseHead = buffer.substr(0, headerEnd);
	buffer.erase(0, headerEnd + 4);
	if (responseHead.find(" 101") == std::string::npos) {
		finish("server refused upgrade: " + responseHead.substr(0, responseHead.find("\r\n")));
		return;
	}

	QByteArray expectedAccept =
		QCryptographicHash::hash(QByteArray::fromStdString(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"),
					 QCryptographicHash::Sha1)
			.toBase64();
	QByteArray head = QByteArray::fromStdString(responseHead).toLower();
	if (!head.contains(expectedAccept.toLower())) {
		finish("invalid Sec-WebSocket-Accept");
		return;
	}

	isConnected = true;
	if (handlers.onOpen)
		handlers.onOpen();

	std::string message;
	uint8_t messageOpcode = 0;
	std::string closeReason = "closed";

	while (!stopRequested) {
		std::deque<std::string> toSend;
		{
			std::lock_guard lock(queueMutex);
			toSend.swap(outgoing);
		}
		bool sendFailed = false;
		for (const auto &frame : toSend) {
			if (!conn.sendAll(frame)) {
				sendFailed = true;
				break;
			}
		}
		if (sendFailed) {
			closeReason = "send failed: " + conn.error;
			break;
		}

		if (!conn.receive(buffer, 100)) {
			closeReason = conn.error;
			break;
		}

		bool closing = false;
		for (;;) {
			if (buffer.size() < 2)
				break;
			uint8_t b0 = (uint8_t)buffer[0];
			uint8_t b1 = (uint8_t)buffer[1];
			bool fin = b0 & 0x80;
			uint8_t opcode = b0 & 0x0F;
			bool masked = b1 & 0x80;
			uint64_t len = b1 & 0x7F;
			size_t pos = 2;

			if (len == 126) {
				if (buffer.size() < 4)
					break;
				len = ((uint8_t)buffer[2] << 8) | (uint8_t)buffer[3];
				pos = 4;
			} else if (len == 127) {
				if (buffer.size() < 10)
					break;
				len = 0;
				for (int i = 0; i < 8; i++)
					len = (len << 8) | (uint8_t)buffer[2 + i];
				pos = 10;
			}

			char mask[4] = {};
			if (masked) {
				if (buffer.size() < pos + 4)
					break;
				memcpy(mask, buffer.data() + pos, 4);
				pos += 4;
			}
			if (buffer.size() < pos + len)
				break;

			std::string payload = buffer.substr(pos, (size_t)len);
			buffer.erase(0, pos + (size_t)len);
			if (masked) {
				for (size_t i = 0; i < payload.size(); i++)
					payload[i] ^= mask[i % 4];
			}

			switch (opcode) {
			case OpText:
			case OpBinary:
				message = payload;
				messageOpcode = opcode;
				break;
			case OpContinuation:
				message += payload;
				break;
			case OpPing: {
				std::lock_guard lock(queueMutex);
				outgoing.push_back(buildFrame(OpPong, payload));
				continue;
			}
			case OpPong:
				continue;
			case OpClose: {
				uint16_t code = payload.size() >= 2
							? (uint16_t)(((uint8_t)payload[0] << 8) | (uint8_t)payload[1])
							: 1005;
				closeReason = "server closed (" + std::to_string(code) + ")";
				if (payload.size() > 2)
					closeReason += ": " + payload.substr(2);
				conn.sendAll(buildFrame(OpClose, payload.substr(0, 2)), true);
				closing = true;
				break;
			}
			default:
				continue;
			}

			if (closing)
				break;
			if (fin && (opcode == OpText || opcode == OpBinary || opcode == OpContinuation)) {
				if (messageOpcode == OpText && handlers.onMessage)
					handlers.onMessage(message);
				message.clear();
			}
		}
		if (closing)
			break;
	}

	if (stopRequested && isConnected)
		conn.sendAll(buildFrame(OpClose, std::string("\x03\xE8", 2)), true);
	finish(stopRequested ? "closed by client" : closeReason);
}

} // namespace sf::net
