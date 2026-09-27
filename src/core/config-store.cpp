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

#include "core/config-store.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace sf {

ConfigStore &ConfigStore::instance()
{
	static ConfigStore store;
	return store;
}

static QString moduleConfigPath(const char *file)
{
	char *path = obs_module_config_path(file);
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

QString ConfigStore::filePath() const
{
	return moduleConfigPath("accounts.json");
}

QString ConfigStore::presetsDir() const
{
	QString dir = moduleConfigPath("presets");
	QDir().mkpath(dir);
	return dir;
}

void ConfigStore::load()
{
	QFile file(filePath());
	if (!file.open(QIODevice::ReadOnly))
		return;

	QJsonParseError error;
	QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !doc.isObject()) {
		obs_log(LOG_WARNING, "Could not parse %s: %s", filePath().toUtf8().constData(),
			error.errorString().toUtf8().constData());
		return;
	}

	std::lock_guard lock(mutex);
	root = doc.object();
}

void ConfigStore::save()
{
	QByteArray data;
	{
		std::lock_guard lock(mutex);
		data = QJsonDocument(root).toJson(QJsonDocument::Indented);
	}

	QString path = filePath();
	QDir().mkpath(QFileInfo(path).absolutePath());

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "Could not write %s", path.toUtf8().constData());
		return;
	}
	/* Tokens live in here; keep the file private to the user where the OS supports it. */
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	file.write(data);
	file.commit();
}

QJsonObject ConfigStore::section(const QString &name) const
{
	std::lock_guard lock(mutex);
	return root.value(name).toObject();
}

void ConfigStore::setSection(const QString &name, const QJsonObject &value)
{
	{
		std::lock_guard lock(mutex);
		root[name] = value;
	}
	save();
	emit sectionChanged(name);
}

void ConfigStore::updateSection(const QString &name, const QJsonObject &changes)
{
	{
		std::lock_guard lock(mutex);
		QJsonObject current = root.value(name).toObject();
		for (auto it = changes.begin(); it != changes.end(); ++it)
			current[it.key()] = it.value();
		root[name] = current;
	}
	save();
	emit sectionChanged(name);
}

} // namespace sf
