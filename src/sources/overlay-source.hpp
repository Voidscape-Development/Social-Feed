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

#include "core/feed-types.hpp"
#include "sources/design-defaults.hpp"

#include <obs.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <mutex>

namespace sf {

namespace settings_keys {
inline constexpr const char *Width = "width";
inline constexpr const char *Height = "height";
inline constexpr const char *Design = "design";     /* JSON object as string */
inline constexpr const char *Channels = "channels"; /* JSON array of {platform, channel} as string */
inline constexpr const char *PreviewDemo = "preview_demo";
} // namespace settings_keys

/* Shared implementation of the Chat Feed and Event Display sources.
 *
 * Each instance owns a private obs-browser source that loads the overlay page from the local
 * server. Feed items are pushed into that page through obs-browser's per-source
 * "javascript_event" proc handler (a `socialFeed` CustomEvent on window); the page fetches its
 * initial config over HTTP. The browser's audio is rerouted into OBS and re-emitted by this
 * source, so alert sounds show up in the mixer. */
class OverlaySource {
public:
	OverlaySource(OverlayKind kind, obs_data_t *settings, obs_source_t *source);
	~OverlaySource();

	static OverlaySource *fromSource(obs_source_t *source);

	OverlayKind kind() const { return overlayKind; }
	obs_source_t *source() const { return self; }

	void update(obs_data_t *settings);
	void render();
	uint32_t width() const { return cx; }
	uint32_t height() const { return cy; }
	void enumActiveSources(obs_source_enum_proc_t callback, void *param);

	/* Delivers an item to this source only (bypasses channel filtering). */
	void deliver(const FeedItem &item);
	void sendTest();
	void refreshBrowser();

	QByteArray configJson();

	static void getDefaults(OverlayKind kind, obs_data_t *settings);
	static obs_properties_t *getProperties(OverlaySource *self, OverlayKind kind);

private:
	bool accepts(const FeedItem &item);
	void pushEnvelope(const QJsonObject &envelope);
	QJsonObject buildConfig();
	void createBrowser();
	static void audioCapture(void *param, obs_source_t *source, const struct audio_data *data, bool muted);

	const OverlayKind overlayKind;
	obs_source_t *self;
	obs_source_t *browser = nullptr;
	QString token;
	int busToken = 0;

	uint32_t cx = 0;
	uint32_t cy = 0;

	std::mutex mutex;
	QJsonObject design;
	QJsonArray channels;
	bool demo = false;
};

} // namespace sf
