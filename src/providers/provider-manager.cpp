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

#include "providers/provider-manager.hpp"

#include "core/config-store.hpp"
#include "providers/stub-providers.hpp"
#include "providers/twitch/twitch-provider.hpp"

#include <QMetaObject>

namespace sf {

QString stateLabel(ProviderStatus::State state)
{
	switch (state) {
	case ProviderStatus::State::Disabled:
		return QStringLiteral("Not connected");
	case ProviderStatus::State::Connecting:
		return QStringLiteral("Connecting");
	case ProviderStatus::State::Connected:
		return QStringLiteral("Connected");
	case ProviderStatus::State::Error:
		return QStringLiteral("Error");
	case ProviderStatus::State::NotImplemented:
		return QStringLiteral("Coming soon");
	}
	return {};
}

ProviderManager &ProviderManager::instance()
{
	static ProviderManager manager;
	return manager;
}

ProviderManager::ProviderManager()
{
	list.push_back(std::make_unique<twitch::TwitchProvider>());
	for (auto &stub : makeStubProviders())
		list.push_back(std::move(stub));

	for (auto &provider : list) {
		QString id = provider->id();
		provider->setStatusCallback([this, id](const ProviderStatus &status) {
			{
				std::lock_guard lock(mutex);
				statuses[id] = status;
			}
			QMetaObject::invokeMethod(this, [this, id]() { emit statusChanged(id); }, Qt::QueuedConnection);
		});
	}

	applyTimer.setSingleShot(true);
	applyTimer.setInterval(250);
	connect(&applyTimer, &QTimer::timeout, this, &ProviderManager::applyChannels);

	connect(&ConfigStore::instance(), &ConfigStore::sectionChanged, this, [this](const QString &section) {
		if (Provider *p = provider(section))
			p->reloadConfig();
	});
}

void ProviderManager::startAll()
{
	for (auto &provider : list)
		provider->start();
	applyChannels();
}

void ProviderManager::stopAll()
{
	applyTimer.stop();
	for (auto &provider : list)
		provider->stop();
}

Provider *ProviderManager::provider(const QString &id) const
{
	for (auto &provider : list) {
		if (provider->id() == id)
			return provider.get();
	}
	return nullptr;
}

ProviderStatus ProviderManager::status(const QString &id) const
{
	std::lock_guard lock(mutex);
	return statuses.value(id);
}

void ProviderManager::setSourceChannels(const QString &sourceToken, const QList<ChannelRef> &channels)
{
	{
		std::lock_guard lock(mutex);
		if (sourceChannels.value(sourceToken) == channels)
			return;
		sourceChannels[sourceToken] = channels;
	}
	QMetaObject::invokeMethod(this, [this]() { applyTimer.start(); }, Qt::QueuedConnection);
}

void ProviderManager::removeSource(const QString &sourceToken)
{
	{
		std::lock_guard lock(mutex);
		if (!sourceChannels.remove(sourceToken))
			return;
	}
	QMetaObject::invokeMethod(this, [this]() { applyTimer.start(); }, Qt::QueuedConnection);
}

void ProviderManager::applyChannels()
{
	QMap<QString, QStringList> byPlatform;
	{
		std::lock_guard lock(mutex);
		for (const auto &channels : sourceChannels) {
			for (const auto &ref : channels) {
				QStringList &target = byPlatform[ref.platform];
				if (!ref.channel.isEmpty() && !target.contains(ref.channel))
					target.append(ref.channel);
			}
		}
	}

	for (auto &provider : list)
		provider->setChannels(byPlatform.value(provider->id()));
}

} // namespace sf
