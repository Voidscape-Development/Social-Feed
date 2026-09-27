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

#include "sources/source-registry.hpp"

#include "sources/overlay-source.hpp"

#include <obs-module.h>

namespace sf {

namespace {

template<OverlayKind Kind> struct SourceType {
	static const char *name(void *)
	{
		return obs_module_text(Kind == OverlayKind::Chat ? "ChatFeed" : "EventDisplay");
	}

	static void *create(obs_data_t *settings, obs_source_t *source)
	{
		return new OverlaySource(Kind, settings, source);
	}

	static void destroy(void *data) { delete static_cast<OverlaySource *>(data); }

	static void update(void *data, obs_data_t *settings) { static_cast<OverlaySource *>(data)->update(settings); }

	static void defaults(obs_data_t *settings) { OverlaySource::getDefaults(Kind, settings); }

	static obs_properties_t *properties(void *data)
	{
		return OverlaySource::getProperties(static_cast<OverlaySource *>(data), Kind);
	}

	static uint32_t width(void *data) { return static_cast<OverlaySource *>(data)->width(); }
	static uint32_t height(void *data) { return static_cast<OverlaySource *>(data)->height(); }

	static void render(void *data, gs_effect_t *) { static_cast<OverlaySource *>(data)->render(); }

	static void enumActive(void *data, obs_source_enum_proc_t callback, void *param)
	{
		static_cast<OverlaySource *>(data)->enumActiveSources(callback, param);
	}

	static obs_source_info info()
	{
		obs_source_info si = {};
		si.id = Kind == OverlayKind::Chat ? kChatSourceId : kEventSourceId;
		si.type = OBS_SOURCE_TYPE_INPUT;
		si.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW |
				  OBS_SOURCE_DO_NOT_DUPLICATE;
		si.icon_type = Kind == OverlayKind::Chat ? OBS_ICON_TYPE_TEXT : OBS_ICON_TYPE_BROWSER;
		si.get_name = name;
		si.create = create;
		si.destroy = destroy;
		si.update = update;
		si.get_defaults = defaults;
		si.get_properties = properties;
		si.get_width = width;
		si.get_height = height;
		si.video_render = render;
		si.enum_active_sources = enumActive;
		return si;
	}
};

} // namespace

void registerSources()
{
	obs_source_info chat = SourceType<OverlayKind::Chat>::info();
	obs_register_source(&chat);

	obs_source_info events = SourceType<OverlayKind::Events>::info();
	obs_register_source(&events);
}

} // namespace sf
