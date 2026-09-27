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

#include "sources/design-defaults.hpp"

#include <QJsonDocument>

namespace sf {

namespace {

const char *kChatDefaults = R"json({
  "version": 1,
  "layout": {
    "direction": "newest-bottom",
    "align": "left",
    "messageLayout": "inline",
    "gap": 8,
    "padding": 12,
    "showBadges": true,
    "showPlatformIcon": true,
    "showAvatars": false,
    "showTimestamps": false,
    "nameSuffix": ":"
  },
  "text": {
    "fontFamily": "Inter, 'Segoe UI', Roboto, sans-serif",
    "fontSize": 20,
    "fontWeight": 500,
    "lineHeight": 1.35,
    "color": "#FFFFFF",
    "nameFontWeight": 800,
    "nameColorMode": "user",
    "nameColor": "#A970FF",
    "emoteScale": 1.4,
    "shadow": true,
    "shadowColor": "#000000B3",
    "shadowBlur": 4,
    "outlineWidth": 0,
    "outlineColor": "#000000"
  },
  "bubble": {
    "enabled": true,
    "background": "#000000A0",
    "radius": 12,
    "padding": 10,
    "borderWidth": 0,
    "borderColor": "#FFFFFF33",
    "accent": "platform",
    "accentWidth": 4,
    "maxWidth": 100
  },
  "animation": {
    "in": "slide-left",
    "inDuration": 350,
    "inEasing": "ease-out",
    "out": "fade",
    "outDuration": 300,
    "outEasing": "ease-in",
    "reflow": true
  },
  "lifetime": {
    "maxMessages": 30,
    "expireSeconds": 0,
    "removeOverflow": true
  },
  "filters": {
    "platforms": { "twitch": true, "youtube": true, "kick": true, "tiktok": true },
    "hideCommands": true,
    "commandPrefixes": "!",
    "hideBots": true,
    "bots": ["nightbot", "streamelements", "streamlabs", "moobot", "fossabot", "sery_bot", "wizebot", "botrixoficial", "kickbot"],
    "blockedUsers": [],
    "blockedWords": [],
    "blockedWordAction": "hide",
    "links": "show",
    "minRole": "everyone",
    "hideEmoteOnly": false,
    "minLength": 0
  },
  "highlights": {
    "firstTime": { "enabled": true, "color": "#FFC53D" },
    "mention": { "enabled": true, "color": "#FF4D6D", "keywords": [] },
    "broadcaster": { "enabled": true, "color": "#E91916" },
    "moderator": { "enabled": false, "color": "#00AD03" },
    "vip": { "enabled": false, "color": "#E005B9" },
    "subscriber": { "enabled": false, "color": "#8205B4" },
    "cheer": { "enabled": true, "color": "#9147FF" },
    "pronouns": false
  },
  "emotes": { "bttv": true, "ffz": true, "sevenTv": true, "animated": true },
  "customCss": ""
})json";

const char *kEventDefaults = R"json({
  "version": 1,
  "layout": {
    "horizontal": "center",
    "vertical": "center",
    "style": "card",
    "mediaPosition": "top",
    "mediaSize": 180,
    "padding": 20,
    "gap": 12,
    "maxWidth": 100,
    "textAlign": "center"
  },
  "text": {
    "fontFamily": "Inter, 'Segoe UI', Roboto, sans-serif",
    "titleSize": 34,
    "titleWeight": 800,
    "titleColor": "#FFFFFF",
    "highlightColor": "#A970FF",
    "messageSize": 20,
    "messageColor": "#E6E6E6",
    "shadow": true,
    "shadowColor": "#000000B3",
    "textAnimation": "none"
  },
  "card": {
    "background": "#0E0E10E6",
    "radius": 16,
    "borderWidth": 2,
    "borderColor": "#A970FF"
  },
  "animation": {
    "in": "zoom",
    "inDuration": 500,
    "inEasing": "ease-out",
    "out": "fade",
    "outDuration": 400,
    "outEasing": "ease-in"
  },
  "queue": {
    "holdSeconds": 6,
    "gapSeconds": 1,
    "maxQueue": 30,
    "dropLowestWhenFull": true
  },
  "tts": {
    "enabled": false,
    "voice": "Brian",
    "volume": 80,
    "minAmount": 0,
    "maxLength": 200,
    "blockedWords": [],
    "readName": false,
    "delayMs": 800
  },
  "platforms": {
    "twitch": true, "youtube": true, "kick": true, "tiktok": true,
    "streamelements": true, "streamlabs": true, "streamerbot": true
  },
  "types": {
    "follow":       { "enabled": true, "priority": 1, "holdSeconds": 0, "title": "{name} just followed!", "message": "", "media": "", "sound": "", "volume": 70, "tts": false, "minAmount": 0, "variants": [] },
    "subscription": { "enabled": true, "priority": 3, "holdSeconds": 0, "title": "{name} subscribed! ({months} months)", "message": "{message}", "media": "", "sound": "", "volume": 70, "tts": true, "minAmount": 0, "variants": [] },
    "gift_sub":     { "enabled": true, "priority": 4, "holdSeconds": 0, "title": "{name} gifted {count} sub(s)!", "message": "", "media": "", "sound": "", "volume": 70, "tts": false, "minAmount": 0, "variants": [] },
    "cheer":        { "enabled": true, "priority": 3, "holdSeconds": 0, "title": "{name} cheered {amount} bits!", "message": "{message}", "media": "", "sound": "", "volume": 70, "tts": true, "minAmount": 0, "variants": [] },
    "tip":          { "enabled": true, "priority": 5, "holdSeconds": 0, "title": "{name} tipped {formattedAmount}!", "message": "{message}", "media": "", "sound": "", "volume": 70, "tts": true, "minAmount": 0, "variants": [] },
    "raid":         { "enabled": true, "priority": 5, "holdSeconds": 0, "title": "{name} is raiding with {count} viewers!", "message": "", "media": "", "sound": "", "volume": 70, "tts": false, "minAmount": 0, "variants": [] },
    "redemption":   { "enabled": true, "priority": 1, "holdSeconds": 0, "title": "{name} redeemed {reward}", "message": "{message}", "media": "", "sound": "", "volume": 70, "tts": false, "minAmount": 0, "variants": [] },
    "hype_train":   { "enabled": true, "priority": 4, "holdSeconds": 0, "title": "Hype Train level {amount}!", "message": "", "media": "", "sound": "", "volume": 70, "tts": false, "minAmount": 0, "variants": [] },
    "membership":   { "enabled": true, "priority": 3, "holdSeconds": 0, "title": "{name} became a member!", "message": "{message}", "media": "", "sound": "", "volume": 70, "tts": false, "minAmount": 0, "variants": [] },
    "super_chat":   { "enabled": true, "priority": 5, "holdSeconds": 0, "title": "{name} sent {formattedAmount}!", "message": "{message}", "media": "", "sound": "", "volume": 70, "tts": true, "minAmount": 0, "variants": [] },
    "gift":         { "enabled": true, "priority": 2, "holdSeconds": 0, "title": "{name} sent {count}x {reward}", "message": "", "media": "", "sound": "", "volume": 70, "tts": false, "minAmount": 0, "variants": [] }
  },
  "customCss": ""
})json";

const char *kChatThemes = R"json({
  "Default": {},
  "Clean (no bubbles)": {
    "bubble": { "enabled": false, "accent": "none" },
    "text": { "shadow": true, "shadowBlur": 6, "outlineWidth": 1 },
    "animation": { "in": "fade", "out": "fade" }
  },
  "Neon": {
    "bubble": { "background": "#120024CC", "borderWidth": 2, "borderColor": "#00F0FF", "radius": 4, "accent": "none" },
    "text": { "fontFamily": "'Orbitron', 'Segoe UI', sans-serif", "color": "#E0FBFF", "shadowColor": "#00F0FFAA", "shadowBlur": 10, "nameColorMode": "custom", "nameColor": "#FF2BD6" },
    "animation": { "in": "zoom", "out": "slide-right" }
  },
  "Compact": {
    "layout": { "gap": 2, "padding": 6, "showPlatformIcon": false },
    "text": { "fontSize": 16 },
    "bubble": { "enabled": false, "accent": "platform", "accentWidth": 2 },
    "animation": { "in": "slide-up", "inDuration": 200, "out": "fade", "outDuration": 200 }
  },
  "Speech bubbles": {
    "layout": { "messageLayout": "stacked", "showAvatars": true },
    "bubble": { "background": "#FFFFFFF0", "radius": 18, "accent": "none" },
    "text": { "color": "#1B1B1F", "shadow": false },
    "animation": { "in": "pop", "out": "fade" }
  }
})json";

const char *kEventThemes = R"json({
  "Default card": {},
  "Banner": {
    "layout": { "style": "banner", "mediaPosition": "left", "mediaSize": 120, "textAlign": "left", "vertical": "top" },
    "card": { "radius": 0, "borderWidth": 0, "background": "#A970FFE6" },
    "text": { "highlightColor": "#FFFFFF", "titleSize": 28 },
    "animation": { "in": "slide-down", "out": "slide-up" }
  },
  "Minimal": {
    "layout": { "style": "minimal", "mediaPosition": "none" },
    "card": { "background": "#00000000", "borderWidth": 0 },
    "text": { "shadow": true, "textAnimation": "wave" },
    "animation": { "in": "fade", "out": "fade" }
  },
  "Media background": {
    "layout": { "mediaPosition": "background", "mediaSize": 260 },
    "card": { "background": "#00000066", "borderWidth": 0, "radius": 24 },
    "animation": { "in": "flip", "out": "zoom" }
  }
})json";

QJsonObject parse(const char *json)
{
	return QJsonDocument::fromJson(QByteArray(json)).object();
}

} // namespace

QJsonObject defaultDesign(OverlayKind kind)
{
	static const QJsonObject chat = parse(kChatDefaults);
	static const QJsonObject events = parse(kEventDefaults);
	return kind == OverlayKind::Chat ? chat : events;
}

QJsonArray defaultChannels(OverlayKind kind)
{
	/* An empty channel means "the logged-in account's own channel". */
	QJsonArray channels;
	channels.append(QJsonObject{{"platform", "twitch"}, {"channel", ""}});
	if (kind == OverlayKind::Events) {
		for (const char *service : {"streamelements", "streamlabs", "streamerbot"})
			channels.append(QJsonObject{{"platform", service}, {"channel", ""}});
	}
	return channels;
}

QJsonObject deepMerge(const QJsonObject &base, const QJsonObject &overrides)
{
	QJsonObject result = base;
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		QJsonValue current = result.value(it.key());
		if (current.isObject() && it.value().isObject())
			result[it.key()] = deepMerge(current.toObject(), it.value().toObject());
		else
			result[it.key()] = it.value();
	}
	return result;
}

QJsonObject resolveDesign(OverlayKind kind, const QJsonObject &stored)
{
	return deepMerge(defaultDesign(kind), stored);
}

QJsonObject builtinThemes(OverlayKind kind)
{
	static const QJsonObject chat = parse(kChatThemes);
	static const QJsonObject events = parse(kEventThemes);
	return kind == OverlayKind::Chat ? chat : events;
}

} // namespace sf
