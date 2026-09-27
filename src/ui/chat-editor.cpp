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

#include "ui/chat-editor.hpp"

#include "ui/editor-widgets.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QObject>
#include <QTabWidget>

namespace sf::ui {

void buildChatTabs(QTabWidget *tabs, DesignModel *model)
{
	auto tr = [](const char *text) {
		return QObject::tr(text);
	};
	QFormLayout *form = nullptr;

	/* Layout */
	tabs->addTab(makeFormPage(form), tr("Layout"));
	bindCombo(form, model, tr("Newest message"), "layout.direction",
		  {{"newest-bottom", tr("At the bottom (messages rise)")},
		   {"newest-top", tr("At the top (messages drop)")}});
	bindCombo(form, model, tr("Alignment"), "layout.align",
		  {{"left", tr("Left")}, {"center", tr("Center")}, {"right", tr("Right")}});
	bindCombo(form, model, tr("Message layout"), "layout.messageLayout",
		  {{"inline", tr("Name and message on one line")}, {"stacked", tr("Name above message")}});
	bindSpin(form, model, tr("Space between messages"), "layout.gap", 0, 100, " px");
	bindSpin(form, model, tr("Edge padding"), "layout.padding", 0, 200, " px");
	addSection(form, tr("Show"));
	bindCheck(form, model, tr("Badges"), "layout.showBadges");
	bindCheck(form, model, tr("Platform icon"), "layout.showPlatformIcon");
	bindCheck(form, model, tr("Avatars (when available)"), "layout.showAvatars");
	bindCheck(form, model, tr("Timestamps"), "layout.showTimestamps");
	bindText(form, model, tr("Text after name"), "layout.nameSuffix", ":");

	/* Text */
	tabs->addTab(makeFormPage(form), tr("Text"));
	bindFont(form, model, tr("Font"), "text.fontFamily");
	bindSpin(form, model, tr("Size"), "text.fontSize", 6, 200, " px");
	bindSpin(form, model, tr("Weight"), "text.fontWeight", 100, 900);
	bindDouble(form, model, tr("Line height"), "text.lineHeight", 0.8, 3.0, 0.05);
	bindColor(form, model, tr("Message color"), "text.color");
	addSection(form, tr("Names"));
	bindSpin(form, model, tr("Name weight"), "text.nameFontWeight", 100, 900);
	bindCombo(form, model, tr("Name color"), "text.nameColorMode",
		  {{"user", tr("User's chat color")}, {"platform", tr("Platform color")}, {"custom", tr("Custom")}});
	bindColor(form, model, tr("Custom name color"), "text.nameColor");
	addSection(form, tr("Effects"));
	bindDouble(form, model, tr("Emote size"), "text.emoteScale", 0.5, 5.0, 0.1, QObject::tr("× text"));
	bindCheck(form, model, tr("Text shadow"), "text.shadow");
	bindColor(form, model, tr("Shadow color"), "text.shadowColor");
	bindSpin(form, model, tr("Shadow blur"), "text.shadowBlur", 0, 40, " px");
	bindSpin(form, model, tr("Outline width"), "text.outlineWidth", 0, 10, " px");
	bindColor(form, model, tr("Outline color"), "text.outlineColor");

	/* Bubbles */
	tabs->addTab(makeFormPage(form), tr("Bubbles"));
	bindCheck(form, model, tr("Draw a bubble behind each message"), "bubble.enabled");
	bindColor(form, model, tr("Background"), "bubble.background");
	bindSpin(form, model, tr("Corner radius"), "bubble.radius", 0, 100, " px");
	bindSpin(form, model, tr("Inner padding"), "bubble.padding", 0, 100, " px");
	bindSpin(form, model, tr("Border width"), "bubble.borderWidth", 0, 20, " px");
	bindColor(form, model, tr("Border color"), "bubble.borderColor");
	bindCombo(form, model, tr("Accent stripe"), "bubble.accent",
		  {{"none", tr("None")}, {"platform", tr("Platform color")}, {"user", tr("User color")}});
	bindSpin(form, model, tr("Accent width"), "bubble.accentWidth", 0, 20, " px");
	bindSpin(form, model, tr("Max width"), "bubble.maxWidth", 20, 100, " %");

	/* Animation */
	tabs->addTab(makeFormPage(form), tr("Animation"));
	addSection(form, tr("Incoming messages"));
	bindCombo(form, model, tr("Effect"), "animation.in", animationInOptions());
	bindSpin(form, model, tr("Duration"), "animation.inDuration", 0, 5000, " ms");
	bindCombo(form, model, tr("Easing"), "animation.inEasing", easingOptions());
	addSection(form, tr("Outgoing messages"));
	bindCombo(form, model, tr("Effect"), "animation.out", animationOutOptions());
	bindSpin(form, model, tr("Duration"), "animation.outDuration", 0, 5000, " ms");
	bindCombo(form, model, tr("Easing"), "animation.outEasing", easingOptions());
	bindCheck(form, model, tr("Smoothly slide other messages into place"), "animation.reflow");

	/* Message lifetime */
	tabs->addTab(makeFormPage(form), tr("Messages"));
	auto *hint = new QLabel(tr("Messages leave the feed by whichever rule triggers first. Use 0 to disable a "
				   "rule."));
	hint->setWordWrap(true);
	form->addRow(hint);
	bindSpin(form, model, tr("Keep at most"), "lifetime.maxMessages", 0, 500, tr(" messages"));
	bindSpin(form, model, tr("Remove after"), "lifetime.expireSeconds", 0, 3600, " s");
	bindCheck(form, model, tr("Remove messages that overflow the source"), "lifetime.removeOverflow");

	/* Filters */
	tabs->addTab(makeFormPage(form), tr("Filters"));
	addSection(form, tr("Platforms"));
	bindCheck(form, model, tr("Twitch"), "filters.platforms.twitch");
	bindCheck(form, model, tr("YouTube"), "filters.platforms.youtube");
	bindCheck(form, model, tr("Kick"), "filters.platforms.kick");
	bindCheck(form, model, tr("TikTok"), "filters.platforms.tiktok");
	addSection(form, tr("Who"));
	bindCombo(form, model, tr("Minimum role"), "filters.minRole",
		  {{"everyone", tr("Everyone")},
		   {"subscriber", tr("Subscribers / members and up")},
		   {"vip", tr("VIPs and up")},
		   {"moderator", tr("Moderators and up")},
		   {"broadcaster", tr("Broadcaster only")}});
	bindCheck(form, model, tr("Hide bots"), "filters.hideBots");
	bindList(form, model, tr("Bot accounts"), "filters.bots");
	bindList(form, model, tr("Blocked users"), "filters.blockedUsers");
	addSection(form, tr("What"));
	bindCheck(form, model, tr("Hide commands"), "filters.hideCommands");
	bindText(form, model, tr("Command prefixes"), "filters.commandPrefixes", "!");
	bindList(form, model, tr("Blocked words"), "filters.blockedWords");
	bindCombo(form, model, tr("When a blocked word is found"), "filters.blockedWordAction",
		  {{"hide", tr("Hide the message")}, {"censor", tr("Replace the word with ***")}});
	bindCombo(form, model, tr("Links"), "filters.links",
		  {{"show", tr("Show")}, {"redact", tr("Replace with <link>")}, {"hide-message", tr("Hide message")}});
	bindCheck(form, model, tr("Hide emote-only messages"), "filters.hideEmoteOnly");
	bindSpin(form, model, tr("Minimum length"), "filters.minLength", 0, 500, tr(" characters"));

	/* Highlights */
	tabs->addTab(makeFormPage(form), tr("Highlights"));
	struct Highlight {
		const char *key;
		const char *label;
	};
	const Highlight highlights[] = {
		{"firstTime", "First-time chatters"},
		{"mention", "Mentions of you / keywords"},
		{"broadcaster", "Broadcaster"},
		{"moderator", "Moderators"},
		{"vip", "VIPs"},
		{"subscriber", "Subscribers / members"},
		{"cheer", "Cheers / paid messages"},
	};
	for (const auto &h : highlights) {
		QString base = QStringLiteral("highlights.%1.").arg(h.key);
		addSection(form, QObject::tr(h.label));
		bindCheck(form, model, tr("Highlight"), base + "enabled");
		bindColor(form, model, tr("Color"), base + "color");
		if (QString(h.key) == "mention")
			bindList(form, model, tr("Extra keywords"), base + "keywords");
	}
	addSection(form, tr("Extras"));
	bindCheck(form, model, tr("Show pronouns (Twitch, via pronouns.alejo.io)"), "highlights.pronouns");

	/* Emotes */
	tabs->addTab(makeFormPage(form), tr("Emotes"));
	auto *emoteHint = new QLabel(tr("Native platform emotes are always shown. Third-party emotes are loaded "
					"for each Twitch channel."));
	emoteHint->setWordWrap(true);
	form->addRow(emoteHint);
	bindCheck(form, model, tr("BetterTTV"), "emotes.bttv");
	bindCheck(form, model, tr("FrankerFaceZ"), "emotes.ffz");
	bindCheck(form, model, tr("7TV"), "emotes.sevenTv");
	bindCheck(form, model, tr("Animate emotes"), "emotes.animated");
}

} // namespace sf::ui
