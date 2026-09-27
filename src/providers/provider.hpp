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
#include <QStringList>

#include <functional>

namespace sf {

struct ProviderStatus {
	enum class State { Disabled, Connecting, Connected, Error, NotImplemented };

	State state = State::Disabled;
	QString message;
};

QString stateLabel(ProviderStatus::State state);

/* A connection to one platform or service. Providers normalize whatever they receive into
 * FeedItems and publish them on the EventBus. start()/stop()/setChannels() are called from the
 * UI thread and must not block; providers run their own worker threads. */
class Provider {
public:
	using StatusCallback = std::function<void(const ProviderStatus &)>;

	virtual ~Provider() = default;

	virtual QString id() const = 0;
	virtual QString displayName() const = 0;
	/* True when the provider produces chat messages (Chat Feed channel list shows it). */
	virtual bool supportsChat() const = 0;

	virtual void start() = 0;
	virtual void stop() = 0;
	/* Channels (lowercase logins/ids) some source wants chat from, in addition to the
	 * account's own channel. */
	virtual void setChannels(const QStringList &channels) { (void)channels; }
	/* Called when the provider's ConfigStore section changed (login, logout, keys). */
	virtual void reloadConfig() {}

	void setStatusCallback(StatusCallback callback) { statusCallback = std::move(callback); }

protected:
	void reportStatus(ProviderStatus::State state, const QString &message = QString())
	{
		if (statusCallback)
			statusCallback(ProviderStatus{state, message});
	}

private:
	StatusCallback statusCallback;
};

} // namespace sf
