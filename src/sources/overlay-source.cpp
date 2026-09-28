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

#include "sources/overlay-source.hpp"

#include "core/config-store.hpp"
#include "core/event-bus.hpp"
#include "core/sample-data.hpp"
#include "net/local-server.hpp"
#include "providers/provider-manager.hpp"
#include "ui/design-editor.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonDocument>
#include <QUuid>

#include <algorithm>
#include <cstring>

namespace sf {

namespace {

QJsonObject parseObject(const char *json)
{
	return json && *json ? QJsonDocument::fromJson(QByteArray(json)).object() : QJsonObject();
}

QJsonArray parseArray(const char *json)
{
	return json && *json ? QJsonDocument::fromJson(QByteArray(json)).array() : QJsonArray();
}

QString ownChannel(const QString &platform)
{
	return ConfigStore::instance().section(platform).value("login").toString().toLower();
}

} // namespace

OverlaySource::OverlaySource(OverlayKind kind, obs_data_t *settings, obs_source_t *source)
	: overlayKind(kind),
	  self(source),
	  token(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
	net::LocalServer::instance().registerSource(token, [this]() { return configJson(); });
	update(settings);
	createBrowser();

	busToken = EventBus::instance().subscribe([this](const FeedItem &item) {
		if (accepts(item))
			pushEnvelope(toEnvelope(item));
	});
}

OverlaySource::~OverlaySource()
{
	/* Unsubscribe first: it blocks until any in-flight delivery to this source has finished. */
	EventBus::instance().unsubscribe(busToken);
	net::LocalServer::instance().unregisterSource(token);
	ProviderManager::instance().removeSource(token);

	if (browser) {
		obs_source_remove_audio_capture_callback(browser, audioCapture, this);
		obs_source_release(browser);
		browser = nullptr;
	}
}

OverlaySource *OverlaySource::fromSource(obs_source_t *source)
{
	if (!source)
		return nullptr;
	const char *id = obs_source_get_unversioned_id(source);
	if (!id || (strcmp(id, "social_feed_chat") != 0 && strcmp(id, "social_feed_events") != 0))
		return nullptr;
	return static_cast<OverlaySource *>(obs_obj_get_data(source));
}

void OverlaySource::createBrowser()
{
	QString page = overlayKind == OverlayKind::Chat ? "chat.html" : "events.html";
	QString url = net::LocalServer::instance().overlayUrl(page, token);

	obs_data_t *bs = obs_data_create();
	obs_data_set_bool(bs, "is_local_file", false);
	obs_data_set_string(bs, "url", url.toUtf8().constData());
	obs_data_set_int(bs, "width", cx);
	obs_data_set_int(bs, "height", cy);
	obs_data_set_bool(bs, "reroute_audio", true);
	obs_data_set_bool(bs, "shutdown", false);
	obs_data_set_bool(bs, "restart_when_active", false);
	obs_data_set_string(bs, "css", "");

	QString name = QStringLiteral("%1 (browser)").arg(QString::fromUtf8(obs_source_get_name(self)));
	browser = obs_source_create_private("browser_source", name.toUtf8().constData(), bs);
	obs_data_release(bs);

	if (!browser) {
		obs_log(LOG_ERROR, "obs-browser is not available; Social Feed sources cannot render");
		return;
	}
	obs_source_add_audio_capture_callback(browser, audioCapture, this);
}

void OverlaySource::audioCapture(void *param, obs_source_t *, const struct audio_data *data, bool muted)
{
	auto *overlay = static_cast<OverlaySource *>(param);
	if (muted || !data || !data->frames)
		return;

	const struct audio_output_info *info = audio_output_get_info(obs_get_audio());
	struct obs_source_audio out = {};
	for (size_t i = 0; i < MAX_AV_PLANES; i++)
		out.data[i] = data->data[i];
	out.frames = data->frames;
	out.timestamp = data->timestamp;
	out.format = AUDIO_FORMAT_FLOAT_PLANAR;
	out.speakers = info->speakers;
	out.samples_per_sec = info->samples_per_sec;
	obs_source_output_audio(overlay->self, &out);
}

void OverlaySource::update(obs_data_t *settings)
{
	uint32_t newCx = (uint32_t)std::max<long long>(16, obs_data_get_int(settings, settings_keys::Width));
	uint32_t newCy = (uint32_t)std::max<long long>(16, obs_data_get_int(settings, settings_keys::Height));

	QJsonArray newChannels = parseArray(obs_data_get_string(settings, settings_keys::Channels));
	if (newChannels.isEmpty())
		newChannels = defaultChannels(overlayKind);

	{
		std::lock_guard lock(mutex);
		design = resolveDesign(overlayKind, parseObject(obs_data_get_string(settings, settings_keys::Design)));
		channels = newChannels;
		demo = obs_data_get_bool(settings, settings_keys::PreviewDemo);
	}

	bool resized = newCx != cx || newCy != cy;
	cx = newCx;
	cy = newCy;
	if (browser && resized) {
		obs_data_t *bs = obs_source_get_settings(browser);
		obs_data_set_int(bs, "width", cx);
		obs_data_set_int(bs, "height", cy);
		obs_source_update(browser, bs);
		obs_data_release(bs);
	}

	/* Explicit channels make the providers join them; "" (own channel) is implied. */
	QList<ChannelRef> refs;
	for (const QJsonValue value : newChannels) {
		QJsonObject c = value.toObject();
		QString channel = c.value("channel").toString().trimmed().toLower();
		if (!channel.isEmpty())
			refs.append({c.value("platform").toString(), channel});
	}
	ProviderManager::instance().setSourceChannels(token, refs);

	if (browser)
		pushEnvelope(QJsonObject{{"type", "config"}, {"payload", buildConfig()}});
}

void OverlaySource::render()
{
	if (browser)
		obs_source_video_render(browser);
}

void OverlaySource::enumActiveSources(obs_source_enum_proc_t callback, void *param)
{
	if (browser)
		callback(self, browser, param);
}

bool OverlaySource::accepts(const FeedItem &item)
{
	if (overlayKind == OverlayKind::Chat && item.kind == FeedKind::Event)
		return false;
	if (overlayKind == OverlayKind::Events && item.kind != FeedKind::Event)
		return false;
	if (item.payload.value("test").toBool())
		return true;

	std::lock_guard lock(mutex);
	for (const QJsonValue value : channels) {
		QJsonObject ref = value.toObject();
		if (ref.value("platform").toString() != item.platform)
			continue;
		QString channel = ref.value("channel").toString().trimmed().toLower();
		if (channel.isEmpty()) {
			/* Own account: services without channels (tips) always match. */
			if (item.channel.isEmpty() || item.channel == ownChannel(item.platform))
				return true;
		} else if (channel == item.channel) {
			return true;
		}
	}
	return false;
}

void OverlaySource::pushEnvelope(const QJsonObject &envelope)
{
	if (!browser)
		return;

	QByteArray json = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", "socialFeed");
	calldata_set_string(&cd, "jsonString", json.constData());
	proc_handler_call(obs_source_get_proc_handler(browser), "javascript_event", &cd);
	calldata_free(&cd);
}

void OverlaySource::deliver(const FeedItem &item)
{
	pushEnvelope(toEnvelope(item));
}

void OverlaySource::sendTest()
{
	if (overlayKind == OverlayKind::Chat) {
		deliver(sampleChatMessage());
		return;
	}
	const QStringList &types = allEventTypes();
	static int next = 0;
	deliver(sampleEvent(types[next++ % types.size()]));
}

void OverlaySource::refreshBrowser()
{
	if (!browser)
		return;
	obs_properties_t *props = obs_source_properties(browser);
	obs_property_t *refresh = obs_properties_get(props, "refreshnocache");
	if (refresh)
		obs_property_button_clicked(refresh, browser);
	obs_properties_destroy(props);
}

QJsonObject OverlaySource::buildConfig()
{
	QJsonObject resolved;
	QJsonArray channelList;
	bool demoMode;
	{
		std::lock_guard lock(mutex);
		resolved = design;
		channelList = channels;
		demoMode = demo;
	}

	/* Local media paths become URLs served by the local server. */
	if (overlayKind == OverlayKind::Events) {
		auto &server = net::LocalServer::instance();
		QJsonObject types = resolved.value("types").toObject();
		for (auto it = types.begin(); it != types.end(); ++it) {
			QJsonObject type = it.value().toObject();
			type["mediaUrl"] = server.registerMedia(type.value("media").toString());
			type["soundUrl"] = server.registerMedia(type.value("sound").toString());
			QJsonArray variants;
			for (const QJsonValue v : type.value("variants").toArray()) {
				QJsonObject variant = v.toObject();
				variant["mediaUrl"] = server.registerMedia(variant.value("media").toString());
				variant["soundUrl"] = server.registerMedia(variant.value("sound").toString());
				variants.append(variant);
			}
			type["variants"] = variants;
			it.value() = type;
		}
		resolved["types"] = types;
	}

	QJsonObject accounts;
	for (const QString &platform : allPlatforms()) {
		QString login = ownChannel(platform);
		if (!login.isEmpty())
			accounts[platform] = login;
	}

	return QJsonObject{{"kind", overlayKind == OverlayKind::Chat ? "chat" : "events"},
			   {"design", resolved},
			   {"channels", channelList},
			   {"accounts", accounts},
			   {"demo", demoMode},
			   {"version", PLUGIN_VERSION}};
}

QByteArray OverlaySource::configJson()
{
	return QJsonDocument(buildConfig()).toJson(QJsonDocument::Compact);
}

void OverlaySource::getDefaults(OverlayKind kind, obs_data_t *settings)
{
	obs_data_set_default_int(settings, settings_keys::Width, kind == OverlayKind::Chat ? 450 : 800);
	obs_data_set_default_int(settings, settings_keys::Height, kind == OverlayKind::Chat ? 700 : 300);
	obs_data_set_default_string(settings, settings_keys::Design, "{}");
	obs_data_set_default_string(settings, settings_keys::Channels,
				    QJsonDocument(defaultChannels(kind)).toJson(QJsonDocument::Compact).constData());
	obs_data_set_default_bool(settings, settings_keys::PreviewDemo, false);
}

obs_properties_t *OverlaySource::getProperties(OverlaySource *overlay, OverlayKind kind)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_int(props, settings_keys::Width, obs_module_text("Width"), 16, 8192, 1);
	obs_properties_add_int(props, settings_keys::Height, obs_module_text("Height"), 16, 8192, 1);

	obs_properties_add_button2(
		props, "open_editor", obs_module_text("OpenDesignEditor"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			auto *o = static_cast<OverlaySource *>(data);
			if (o)
				ui::openDesignEditor(o->source());
			return false;
		},
		overlay);

	obs_properties_add_button2(
		props, "send_test", obs_module_text(kind == OverlayKind::Chat ? "SendTestMessage" : "SendTestEvent"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			if (auto *o = static_cast<OverlaySource *>(data))
				o->sendTest();
			return false;
		},
		overlay);

	obs_properties_add_button2(
		props, "refresh_browser", obs_module_text("RefreshOverlay"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			if (auto *o = static_cast<OverlaySource *>(data))
				o->refreshBrowser();
			return false;
		},
		overlay);

	obs_properties_add_text(props, "editor_hint", obs_module_text("EditorHint"), OBS_TEXT_INFO);
	return props;
}

} // namespace sf
