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

#include "providers/provider.hpp"

#include <QHash>
#include <QMap>
#include <QObject>
#include <QTimer>

#include <memory>
#include <mutex>
#include <vector>

namespace sf {

/* A channel a source wants to receive items from. */
struct ChannelRef {
	QString platform;
	QString channel; /* lowercase */

	bool operator==(const ChannelRef &other) const
	{
		return platform == other.platform && channel == other.channel;
	}
};

/* Owns all providers and aggregates which channels the overlay sources need, so each platform
 * holds one shared connection regardless of how many sources exist. UI thread only, except
 * status() which is thread-safe. */
class ProviderManager : public QObject {
	Q_OBJECT

public:
	static ProviderManager &instance();

	void startAll();
	void stopAll();

	const std::vector<std::unique_ptr<Provider>> &providers() const { return list; }
	Provider *provider(const QString &id) const;
	ProviderStatus status(const QString &id) const;

	/* Thread-safe; applied on the UI thread after a short debounce. */
	void setSourceChannels(const QString &sourceToken, const QList<ChannelRef> &channels);
	void removeSource(const QString &sourceToken);

signals:
	void statusChanged(const QString &providerId);

private:
	ProviderManager();
	void applyChannels();

	std::vector<std::unique_ptr<Provider>> list;
	QTimer applyTimer;

	mutable std::mutex mutex;
	QHash<QString, ProviderStatus> statuses;
	QHash<QString, QList<ChannelRef>> sourceChannels;
};

} // namespace sf
