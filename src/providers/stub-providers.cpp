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

#include "providers/stub-providers.hpp"

#include "core/feed-types.hpp"

namespace sf {

namespace {

class StubProvider : public Provider {
public:
	StubProvider(QString id, bool chat) : providerId(std::move(id)), chat(chat) {}

	QString id() const override { return providerId; }
	QString displayName() const override { return platformLabel(providerId); }
	bool supportsChat() const override { return chat; }

	void start() override { reportStatus(ProviderStatus::State::NotImplemented); }
	void stop() override {}

private:
	QString providerId;
	bool chat;
};

} // namespace

std::vector<std::unique_ptr<Provider>> makeStubProviders()
{
	std::vector<std::unique_ptr<Provider>> stubs;
	stubs.push_back(std::make_unique<StubProvider>("kick", true));
	stubs.push_back(std::make_unique<StubProvider>("tiktok", true));
	stubs.push_back(std::make_unique<StubProvider>("streamelements", false));
	stubs.push_back(std::make_unique<StubProvider>("streamlabs", false));
	stubs.push_back(std::make_unique<StubProvider>("streamerbot", false));
	return stubs;
}

} // namespace sf
