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
#include <QObject>
#include <QString>

#include <mutex>

namespace sf {

/* Global (not per-source) plugin settings: platform accounts, tokens and service keys.
 * Persisted as JSON in the plugin's OBS config directory. Each top-level key is a section,
 * usually a platform id ("twitch", "streamelements", ...). */
class ConfigStore : public QObject {
	Q_OBJECT

public:
	static ConfigStore &instance();

	void load();
	void save();

	QJsonObject section(const QString &name) const;
	void setSection(const QString &name, const QJsonObject &value);
	/* Merges keys into an existing section. */
	void updateSection(const QString &name, const QJsonObject &changes);

	QString presetsDir() const;

signals:
	void sectionChanged(const QString &name);

private:
	ConfigStore() = default;
	QString filePath() const;

	mutable std::mutex mutex;
	QJsonObject root;
};

} // namespace sf
