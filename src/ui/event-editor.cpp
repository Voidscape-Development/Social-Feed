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

#include "ui/event-editor.hpp"

#include "core/feed-types.hpp"
#include "ui/editor-widgets.hpp"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sf::ui {

namespace {

const QString kMediaFilter =
	QObject::tr("Media (*.png *.jpg *.jpeg *.gif *.webp *.apng *.svg *.webm *.mp4);;All files (*)");
const QString kSoundFilter = QObject::tr("Audio (*.mp3 *.ogg *.wav *.m4a *.webm);;All files (*)");

enum VariantColumn { ColMinAmount, ColTitle, ColMessage, ColMedia, ColSound, ColCount };
const char *kVariantKeys[] = {"minAmount", "title", "message", "media", "sound"};

/* Amount-based variations of one event type: the variant with the highest minAmount not above
 * the event amount wins, and its non-empty fields replace the type's defaults. */
QWidget *buildVariantsEditor(DesignModel *model, const QString &path)
{
	auto *box = new QWidget();
	auto *layout = new QVBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);

	auto *table = new QTableWidget(0, ColCount, box);
	table->setHorizontalHeaderLabels({QObject::tr("Min amount"), QObject::tr("Title"), QObject::tr("Message"),
					  QObject::tr("Media"), QObject::tr("Sound")});
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(ColMinAmount, QHeaderView::ResizeToContents);
	table->verticalHeader()->setVisible(false);
	table->setMinimumHeight(130);
	layout->addWidget(table);

	auto *buttons = new QHBoxLayout();
	auto *add = new QPushButton(QObject::tr("Add variation"));
	auto *remove = new QPushButton(QObject::tr("Remove"));
	auto *media = new QPushButton(QObject::tr("Pick media…"));
	auto *sound = new QPushButton(QObject::tr("Pick sound…"));
	for (auto *b : {add, remove, media, sound})
		buttons->addWidget(b);
	buttons->addStretch();
	layout->addLayout(buttons);

	auto store = [=]() {
		QJsonArray variants;
		for (int row = 0; row < table->rowCount(); row++) {
			QJsonObject variant;
			for (int col = 0; col < ColCount; col++) {
				QString text = table->item(row, col) ? table->item(row, col)->text().trimmed()
								     : QString();
				if (col == ColMinAmount)
					variant[kVariantKeys[col]] = text.toDouble();
				else
					variant[kVariantKeys[col]] = text;
			}
			variants.append(variant);
		}
		model->setValue(path, variants);
	};

	auto reload = [=]() {
		QSignalBlocker block(table);
		QJsonArray variants = model->value(path).toArray();
		table->setRowCount(variants.size());
		for (int row = 0; row < variants.size(); row++) {
			QJsonObject v = variants[row].toObject();
			for (int col = 0; col < ColCount; col++) {
				QJsonValue value = v.value(kVariantKeys[col]);
				QString text = col == ColMinAmount ? QString::number(value.toDouble())
								   : value.toString();
				table->setItem(row, col, new QTableWidgetItem(text));
			}
		}
	};
	reload();
	QObject::connect(model, &DesignModel::reloaded, table, reload);
	QObject::connect(table, &QTableWidget::itemChanged, table, [=]() { store(); });

	QObject::connect(add, &QPushButton::clicked, table, [=]() {
		QSignalBlocker block(table);
		int row = table->rowCount();
		table->insertRow(row);
		table->setItem(row, ColMinAmount, new QTableWidgetItem("100"));
		for (int col = ColTitle; col < ColCount; col++)
			table->setItem(row, col, new QTableWidgetItem());
		store();
	});
	QObject::connect(remove, &QPushButton::clicked, table, [=]() {
		if (table->currentRow() >= 0) {
			table->removeRow(table->currentRow());
			store();
		}
	});
	auto pick = [=](int column, const QString &filter) {
		int row = table->currentRow();
		if (row < 0)
			return;
		QString file = QFileDialog::getOpenFileName(box, QObject::tr("Choose file"), QString(), filter);
		if (!file.isEmpty())
			table->item(row, column)->setText(file);
	};
	QObject::connect(media, &QPushButton::clicked, table, [=]() { pick(ColMedia, kMediaFilter); });
	QObject::connect(sound, &QPushButton::clicked, table, [=]() { pick(ColSound, kSoundFilter); });

	return box;
}

QWidget *buildTypePage(DesignModel *model, const QString &type)
{
	QFormLayout *form = nullptr;
	QWidget *page = makeFormPage(form);
	QString base = QStringLiteral("types.%1.").arg(type);

	auto *title = new QLabel(QStringLiteral("<h3>%1</h3>").arg(eventTypeLabel(type).toHtmlEscaped()));
	form->addRow(title);
	bindCheck(form, model, QObject::tr("Show this event type"), base + "enabled");
	bindSpin(form, model, QObject::tr("Priority"), base + "priority", 0, 10);
	bindSpin(form, model, QObject::tr("Hold time (0 = default)"), base + "holdSeconds", 0, 120, " s");
	bindDouble(form, model, QObject::tr("Minimum amount"), base + "minAmount", 0, 1000000, 1);

	addSection(form, QObject::tr("Text"));
	bindText(form, model, QObject::tr("Title"), base + "title");
	bindText(form, model, QObject::tr("Message"), base + "message");
	auto *tokens = new QLabel(QObject::tr("Placeholders: {name} {amount} {formattedAmount} {count} {months} "
					      "{tier} {reward} {recipient} {message} {platform}"));
	tokens->setWordWrap(true);
	tokens->setStyleSheet("color: gray;");
	form->addRow(tokens);

	addSection(form, QObject::tr("Media"));
	bindFile(form, model, QObject::tr("Image / GIF / video"), base + "media", kMediaFilter);
	bindFile(form, model, QObject::tr("Sound"), base + "sound", kSoundFilter);
	bindSpin(form, model, QObject::tr("Sound volume"), base + "volume", 0, 100, " %");
	bindCheck(form, model, QObject::tr("Read the message with text-to-speech"), base + "tts");

	addSection(form, QObject::tr("Variations by amount"));
	auto *variantHelp = new QLabel(QObject::tr(
		"Use a different title, media or sound when the amount (bits, months, gifted subs, viewers, "
		"tip value…) reaches a threshold. Empty cells keep the defaults above."));
	variantHelp->setWordWrap(true);
	form->addRow(variantHelp);
	form->addRow(buildVariantsEditor(model, base + "variants"));
	return page;
}

} // namespace

void buildEventTabs(QTabWidget *tabs, DesignModel *model)
{
	auto tr = [](const char *text) {
		return QObject::tr(text);
	};
	QFormLayout *form = nullptr;

	/* Layout */
	tabs->addTab(makeFormPage(form), tr("Layout"));
	bindCombo(form, model, tr("Style"), "layout.style",
		  {{"card", tr("Card")}, {"banner", tr("Banner")}, {"minimal", tr("Minimal (text only)")}});
	bindCombo(form, model, tr("Horizontal position"), "layout.horizontal",
		  {{"left", tr("Left")}, {"center", tr("Center")}, {"right", tr("Right")}});
	bindCombo(form, model, tr("Vertical position"), "layout.vertical",
		  {{"top", tr("Top")}, {"center", tr("Center")}, {"bottom", tr("Bottom")}});
	bindCombo(form, model, tr("Text alignment"), "layout.textAlign",
		  {{"left", tr("Left")}, {"center", tr("Center")}, {"right", tr("Right")}});
	bindCombo(form, model, tr("Media position"), "layout.mediaPosition",
		  {{"top", tr("Above text")},
		   {"left", tr("Left of text")},
		   {"right", tr("Right of text")},
		   {"background", tr("Behind text")},
		   {"none", tr("Hidden")}});
	bindSpin(form, model, tr("Media size"), "layout.mediaSize", 16, 2000, " px");
	bindSpin(form, model, tr("Padding"), "layout.padding", 0, 200, " px");
	bindSpin(form, model, tr("Gap"), "layout.gap", 0, 200, " px");
	bindSpin(form, model, tr("Max width"), "layout.maxWidth", 10, 100, " %");

	/* Text & card */
	tabs->addTab(makeFormPage(form), tr("Text && Card"));
	bindFont(form, model, tr("Font"), "text.fontFamily");
	bindSpin(form, model, tr("Title size"), "text.titleSize", 6, 300, " px");
	bindSpin(form, model, tr("Title weight"), "text.titleWeight", 100, 900);
	bindColor(form, model, tr("Title color"), "text.titleColor");
	bindColor(form, model, tr("Highlight color (names, amounts)"), "text.highlightColor");
	bindSpin(form, model, tr("Message size"), "text.messageSize", 6, 200, " px");
	bindColor(form, model, tr("Message color"), "text.messageColor");
	bindCheck(form, model, tr("Text shadow"), "text.shadow");
	bindColor(form, model, tr("Shadow color"), "text.shadowColor");
	bindCombo(form, model, tr("Highlight animation"), "text.textAnimation",
		  {{"none", tr("None")},
		   {"wave", tr("Wave")},
		   {"pulse", tr("Pulse")},
		   {"bounce", tr("Bounce")},
		   {"rainbow", tr("Rainbow")}});
	addSection(form, tr("Card"));
	bindColor(form, model, tr("Background"), "card.background");
	bindSpin(form, model, tr("Corner radius"), "card.radius", 0, 200, " px");
	bindSpin(form, model, tr("Border width"), "card.borderWidth", 0, 30, " px");
	bindColor(form, model, tr("Border color"), "card.borderColor");

	/* Animation */
	tabs->addTab(makeFormPage(form), tr("Animation"));
	addSection(form, tr("Enter"));
	bindCombo(form, model, tr("Effect"), "animation.in", animationInOptions());
	bindSpin(form, model, tr("Duration"), "animation.inDuration", 0, 5000, " ms");
	bindCombo(form, model, tr("Easing"), "animation.inEasing", easingOptions());
	addSection(form, tr("Exit"));
	bindCombo(form, model, tr("Effect"), "animation.out", animationOutOptions());
	bindSpin(form, model, tr("Duration"), "animation.outDuration", 0, 5000, " ms");
	bindCombo(form, model, tr("Easing"), "animation.outEasing", easingOptions());

	/* Queue */
	tabs->addTab(makeFormPage(form), tr("Queue"));
	auto *queueHelp = new QLabel(tr("One event is shown at a time. Waiting events are ordered by priority "
					"(higher first), then by arrival."));
	queueHelp->setWordWrap(true);
	form->addRow(queueHelp);
	bindSpin(form, model, tr("Default hold time"), "queue.holdSeconds", 1, 120, " s");
	bindDouble(form, model, tr("Pause between events"), "queue.gapSeconds", 0, 30, 0.25, " s");
	bindSpin(form, model, tr("Max waiting events"), "queue.maxQueue", 1, 500);
	bindCheck(form, model, tr("When full, drop the lowest-priority event"), "queue.dropLowestWhenFull");

	/* Event types */
	auto *typesPage = new QSplitter(Qt::Horizontal);
	auto *list = new QListWidget();
	auto *stack = new QStackedWidget();
	for (const QString &type : allEventTypes()) {
		list->addItem(eventTypeLabel(type));
		stack->addWidget(buildTypePage(model, type));
	}
	list->setMaximumWidth(220);
	QObject::connect(list, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
	list->setCurrentRow(0);
	typesPage->addWidget(list);
	typesPage->addWidget(stack);
	typesPage->setStretchFactor(1, 1);
	tabs->addTab(typesPage, tr("Event Types"));

	/* TTS */
	tabs->addTab(makeFormPage(form), tr("Text-to-Speech"));
	auto *ttsHelp = new QLabel(tr("Reads the viewer's message for event types that have text-to-speech "
				      "enabled. Speech is played through the source's audio, so it can be mixed and "
				      "monitored in OBS."));
	ttsHelp->setWordWrap(true);
	form->addRow(ttsHelp);
	bindCheck(form, model, tr("Enable text-to-speech"), "tts.enabled");
	bindCombo(form, model, tr("Voice"), "tts.voice",
		  {{"Brian", "Brian (UK English, male)"},
		   {"Amy", "Amy (UK English, female)"},
		   {"Emma", "Emma (UK English, female)"},
		   {"Joey", "Joey (US English, male)"},
		   {"Joanna", "Joanna (US English, female)"},
		   {"Matthew", "Matthew (US English, male)"},
		   {"Salli", "Salli (US English, female)"},
		   {"Justin", "Justin (US English, child)"},
		   {"Russell", "Russell (Australian English)"},
		   {"Nicole", "Nicole (Australian English)"},
		   {"Hans", "Hans (German)"},
		   {"Celine", "Céline (French)"},
		   {"Conchita", "Conchita (Spanish)"},
		   {"Mizuki", "Mizuki (Japanese)"}});
	bindSpin(form, model, tr("Volume"), "tts.volume", 0, 100, " %");
	bindDouble(form, model, tr("Minimum amount"), "tts.minAmount", 0, 1000000, 1);
	bindSpin(form, model, tr("Max characters"), "tts.maxLength", 10, 1000);
	bindSpin(form, model, tr("Delay after alert sound"), "tts.delayMs", 0, 10000, " ms");
	bindCheck(form, model, tr("Read the viewer's name first"), "tts.readName");
	bindList(form, model, tr("Never read messages containing"), "tts.blockedWords");

	/* Platforms */
	tabs->addTab(makeFormPage(form), tr("Platforms"));
	auto *platformHelp = new QLabel(tr("Events from disabled platforms are ignored by this source."));
	platformHelp->setWordWrap(true);
	form->addRow(platformHelp);
	for (const QString &platform : allPlatforms())
		bindCheck(form, model, platformLabel(platform), "platforms." + platform);
}

} // namespace sf::ui
