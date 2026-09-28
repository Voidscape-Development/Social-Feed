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
#include "core/event-bus.hpp"
#include "net/http-client.hpp"
#include "net/local-server.hpp"
#include "providers/provider-manager.hpp"
#include "sources/source-registry.hpp"
#include "ui/design-editor.hpp"
#include "ui/social-feed-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

namespace {

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		sf::ProviderManager::instance().startAll();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		sf::ui::closeAllEditors();
		sf::ProviderManager::instance().stopAll();
		break;
	default:
		break;
	}
}

} // namespace

bool obs_module_load(void)
{
	sf::net::globalInit();
	sf::ConfigStore::instance().load();
	/* These QObjects own timers and queued connections, so they must be created on the UI
	 * thread rather than lazily from whichever thread first updates a source. */
	sf::EventBus::instance();
	sf::ProviderManager::instance();

	/* Must be listening before any source is created so overlay URLs carry the right port. */
	if (!sf::net::LocalServer::instance().start())
		return false;

	sf::registerSources();

	auto *dock = new sf::ui::SocialFeedDock();
	if (!obs_frontend_add_dock_by_id("social-feed-dock", obs_module_text("SocialFeed"), dock))
		delete dock;

	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	sf::ProviderManager::instance().stopAll();
	sf::net::LocalServer::instance().stop();
	sf::net::globalCleanup();
	obs_log(LOG_INFO, "plugin unloaded");
}
