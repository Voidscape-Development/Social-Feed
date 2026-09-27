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

#include "ui/design-editor.hpp"

#include "core/config-store.hpp"
#include "core/feed-types.hpp"
#include "sources/overlay-source.hpp"
#include "sources/source-registry.hpp"
#include "ui/chat-editor.hpp"
#include "ui/editor-widgets.hpp"
#include "ui/event-editor.hpp"
#include "ui/preview-widget.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sf::ui {

namespace {

QHash<QString, QPointer<DesignEditor>> &openEditors()
{
	static QHash<QString, QPointer<DesignEditor>> editors;
	return editors;
}

QJsonObject parseObject(const char *json)
{
	return json && *json ? QJsonDocument::fromJson(QByteArray(json)).object() : QJsonObject();
}

QString toJson(const QJsonObject &object)
{
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString toJson(const QJsonArray &array)
{
	return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QStringList platformsFor(OverlayKind kind)
{
	if (kind == OverlayKind::Chat)
		return {"twitch", "youtube", "kick", "tiktok"};
	return allPlatforms();
}

constexpr int kBuiltinRole = Qt::UserRole + 1;

} // namespace

void openDesignEditor(obs_source_t *source)
{
	if (!OverlaySource::fromSource(source))
		return;

	QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	QPointer<DesignEditor> &editor = openEditors()[uuid];
	if (!editor) {
		auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
		editor = new DesignEditor(source, parent);
		editor->setAttribute(Qt::WA_DeleteOnClose);
	}
	editor->show();
	editor->raise();
	editor->activateWindow();
}

void closeAllEditors()
{
	/* Copy: closing an editor removes it from the map. Delete right away instead of waiting for
	 * deleteLater, so the editors release their source references before OBS shuts down. */
	const auto editors = openEditors().values();
	for (const QPointer<DesignEditor> &editor : editors) {
		if (editor) {
			editor->close();
			delete editor.data();
		}
	}
	openEditors().clear();
}

/* ---- ChannelsEditor ---- */

ChannelsEditor::ChannelsEditor(OverlayKind kind, QWidget *parent) : QWidget(parent), kind(kind)
{
	auto *layout = new QVBoxLayout(this);

	auto *help = new QLabel(
		kind == OverlayKind::Chat
			? tr("Pick the channels this Chat Feed shows. Leave the channel empty to use the account "
			     "you are logged in with; enter any channel name to read another channel's chat "
			     "(Twitch chat can be read without logging in).")
			: tr("Pick where this Event Display takes events from. Leave the channel empty for your own "
			     "account. Other channels only provide the events visible in their chat (subs, gifts, "
			     "raids, bits)."));
	help->setWordWrap(true);
	layout->addWidget(help);

	table = new QTableWidget(0, 2, this);
	table->setHorizontalHeaderLabels({tr("Platform"), tr("Channel")});
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table->verticalHeader()->setVisible(false);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	layout->addWidget(table, 1);

	auto *buttons = new QHBoxLayout();
	auto *add = new QPushButton(tr("Add channel"), this);
	auto *remove = new QPushButton(tr("Remove selected"), this);
	buttons->addWidget(add);
	buttons->addWidget(remove);
	buttons->addStretch();
	layout->addLayout(buttons);

	connect(add, &QPushButton::clicked, this, [this]() {
		addRow("twitch", QString());
		emit changed();
	});
	connect(remove, &QPushButton::clicked, this, [this]() {
		int row = table->currentRow();
		if (row >= 0) {
			table->removeRow(row);
			emit changed();
		}
	});
}

void ChannelsEditor::addRow(const QString &platform, const QString &channel)
{
	int row = table->rowCount();
	table->insertRow(row);

	auto *combo = new QComboBox(table);
	for (const QString &p : platformsFor(kind))
		combo->addItem(platformLabel(p), p);
	combo->setCurrentIndex(std::max(0, combo->findData(platform)));
	connect(combo, &QComboBox::currentIndexChanged, this, &ChannelsEditor::changed);
	table->setCellWidget(row, 0, combo);

	auto *edit = new QLineEdit(channel, table);
	edit->setPlaceholderText(tr("(my account)"));
	connect(edit, &QLineEdit::editingFinished, this, &ChannelsEditor::changed);
	table->setCellWidget(row, 1, edit);
}

QJsonArray ChannelsEditor::channels() const
{
	QJsonArray out;
	for (int row = 0; row < table->rowCount(); row++) {
		auto *combo = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
		auto *edit = qobject_cast<QLineEdit *>(table->cellWidget(row, 1));
		if (!combo || !edit)
			continue;
		QString channel = edit->text().trimmed().toLower();
		if (channel.startsWith('#') || channel.startsWith('@'))
			channel = channel.mid(1);
		out.append(QJsonObject{{"platform", combo->currentData().toString()}, {"channel", channel}});
	}
	return out;
}

void ChannelsEditor::setChannels(const QJsonArray &channels)
{
	table->setRowCount(0);
	for (const auto &value : channels) {
		QJsonObject c = value.toObject();
		addRow(c.value("platform").toString(), c.value("channel").toString());
	}
}

/* ---- DesignEditor ---- */

DesignEditor::DesignEditor(obs_source_t *src, QWidget *parent)
	: QDialog(parent),
	  kind(OverlaySource::fromSource(src)->kind()),
	  source(obs_source_get_ref(src)),
	  model(new DesignModel(this))
{
	setWindowTitle(tr("Social Feed Designer - %1").arg(QString::fromUtf8(obs_source_get_name(source))));
	setWindowFlag(Qt::WindowMaximizeButtonHint);
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	resize(1200, 760);

	originalSettings = obs_source_get_settings(source);
	obs_data_t *copy = obs_data_create();
	obs_data_apply(copy, originalSettings);
	obs_data_release(originalSettings);
	originalSettings = copy;

	signal_handler_connect(obs_source_get_signal_handler(source), "remove", sourceRemoved, this);

	pushTimer.setSingleShot(true);
	pushTimer.setInterval(120);
	connect(&pushTimer, &QTimer::timeout, this, &DesignEditor::pushToPreview);

	buildUi();
	loadFromSource();

	/* The preview is a private copy of the source, so demo data and unapplied changes never
	 * reach the stream. */
	obs_data_t *previewSettings = collectSettings(true);
	preview = obs_source_create_private(obs_source_get_id(source), "Social Feed Preview", previewSettings);
	obs_data_release(previewSettings);
	if (preview) {
		obs_source_inc_showing(preview);
		obs_source_inc_active(preview);
		previewWidget->setSource(preview);
	}

	connect(model, &DesignModel::changed, this, &DesignEditor::schedulePush);
}

DesignEditor::~DesignEditor()
{
	signal_handler_disconnect(obs_source_get_signal_handler(source), "remove", sourceRemoved, this);
	previewWidget->setSource(nullptr);
	if (preview) {
		obs_source_dec_active(preview);
		obs_source_dec_showing(preview);
		obs_source_release(preview);
	}
	obs_data_release(originalSettings);

	QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	openEditors().remove(uuid);
	obs_source_release(source);
}

void DesignEditor::sourceRemoved(void *data, calldata_t *)
{
	auto *editor = static_cast<DesignEditor *>(data);
	QMetaObject::invokeMethod(editor, "close", Qt::QueuedConnection);
}

void DesignEditor::buildUi()
{
	auto *root = new QVBoxLayout(this);

	/* Size + presets */
	auto *top = new QHBoxLayout();
	top->addWidget(new QLabel(tr("Width")));
	widthSpin = new QSpinBox();
	widthSpin->setRange(16, 8192);
	widthSpin->setSuffix(" px");
	top->addWidget(widthSpin);
	top->addWidget(new QLabel(tr("Height")));
	heightSpin = new QSpinBox();
	heightSpin->setRange(16, 8192);
	heightSpin->setSuffix(" px");
	top->addWidget(heightSpin);
	top->addSpacing(24);

	top->addWidget(new QLabel(tr("Preset")));
	presetCombo = new QComboBox();
	presetCombo->setMinimumWidth(200);
	top->addWidget(presetCombo);
	auto *savePresetBtn = new QPushButton(tr("Save as…"));
	auto *deletePresetBtn = new QPushButton(tr("Delete"));
	auto *importBtn = new QPushButton(tr("Import…"));
	auto *exportBtn = new QPushButton(tr("Export…"));
	auto *resetBtn = new QPushButton(tr("Reset"));
	for (auto *b : {savePresetBtn, deletePresetBtn, importBtn, exportBtn, resetBtn})
		top->addWidget(b);
	top->addStretch();
	root->addLayout(top);

	connect(widthSpin, &QSpinBox::valueChanged, this, &DesignEditor::schedulePush);
	connect(heightSpin, &QSpinBox::valueChanged, this, &DesignEditor::schedulePush);
	connect(presetCombo, &QComboBox::activated, this, &DesignEditor::applyPreset);
	connect(savePresetBtn, &QPushButton::clicked, this, &DesignEditor::savePreset);
	connect(deletePresetBtn, &QPushButton::clicked, this, &DesignEditor::deletePreset);
	connect(importBtn, &QPushButton::clicked, this, &DesignEditor::importPreset);
	connect(exportBtn, &QPushButton::clicked, this, &DesignEditor::exportPreset);
	connect(resetBtn, &QPushButton::clicked, this, &DesignEditor::resetDesign);

	/* Controls | preview */
	auto *splitter = new QSplitter(Qt::Horizontal);
	auto *tabs = new QTabWidget();
	tabs->setDocumentMode(true);
	tabs->setUsesScrollButtons(true);
	if (kind == OverlayKind::Chat)
		buildChatTabs(tabs, model);
	else
		buildEventTabs(tabs, model);

	channelsEditor = new ChannelsEditor(kind);
	connect(channelsEditor, &ChannelsEditor::changed, this, &DesignEditor::schedulePush);
	tabs->addTab(channelsEditor, tr("Channels"));

	auto *cssPage = new QWidget();
	auto *cssLayout = new QVBoxLayout(cssPage);
	auto *cssHelp = new QLabel(
		tr("Custom CSS is applied after the generated styles. Useful selectors: %1")
			.arg(kind == OverlayKind::Chat
				     ? "<code>.sf-message, .sf-bubble, .sf-name, .sf-text, .sf-badge, .sf-emote, "
				       ".sf-platform-twitch, .sf-role-moderator, .sf-first-time, .sf-mention</code>"
				     : "<code>.sf-alert, .sf-card, .sf-media, .sf-title, .sf-message, .sf-highlight, "
				       ".sf-type-follow, .sf-type-subscription, .sf-platform-twitch</code>"));
	cssHelp->setWordWrap(true);
	cssHelp->setTextFormat(Qt::RichText);
	auto *cssEdit = new QPlainTextEdit();
	cssEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	cssEdit->setPlaceholderText("/* e.g. */\n.sf-name { text-transform: uppercase; }");
	cssLayout->addWidget(cssHelp);
	cssLayout->addWidget(cssEdit, 1);
	tabs->addTab(cssPage, tr("Custom CSS"));
	auto refreshCss = [this, cssEdit]() {
		QString css = model->value("customCss").toString();
		if (cssEdit->toPlainText() != css)
			cssEdit->setPlainText(css);
	};
	connect(model, &DesignModel::reloaded, cssEdit, refreshCss);
	connect(cssEdit, &QPlainTextEdit::textChanged, this,
		[this, cssEdit]() { model->setValue("customCss", cssEdit->toPlainText()); });

	auto *previewPane = new QWidget();
	auto *previewLayout = new QVBoxLayout(previewPane);
	previewLayout->setContentsMargins(0, 0, 0, 0);
	previewWidget = new PreviewWidget();
	previewLayout->addWidget(previewWidget, 1);

	auto *previewBar = new QHBoxLayout();
	demoMode = new QCheckBox(tr("Demo data"));
	demoMode->setChecked(true);
	demoMode->setToolTip(tr("Continuously feed sample %1 into the preview")
				     .arg(kind == OverlayKind::Chat ? tr("messages") : tr("events")));
	auto *testBtn = new QPushButton(kind == OverlayKind::Chat ? tr("Send test message") : tr("Send test event"));
	auto *bgCombo = new QComboBox();
	bgCombo->addItem(tr("Dark background"));
	bgCombo->addItem(tr("Light background"));
	bgCombo->addItem(tr("Grey background"));
	previewBar->addWidget(demoMode);
	previewBar->addWidget(testBtn);
	previewBar->addStretch();
	previewBar->addWidget(bgCombo);
	previewLayout->addLayout(previewBar);

	connect(demoMode, &QCheckBox::toggled, this, &DesignEditor::schedulePush);
	connect(testBtn, &QPushButton::clicked, this, &DesignEditor::sendPreviewTest);
	connect(bgCombo, &QComboBox::currentIndexChanged, this,
		[this](int index) { previewWidget->setBackground((PreviewWidget::Background)index); });

	splitter->addWidget(tabs);
	splitter->addWidget(previewPane);
	splitter->setStretchFactor(0, 2);
	splitter->setStretchFactor(1, 3);
	root->addWidget(splitter, 1);

	/* Footer */
	auto *footer = new QHBoxLayout();
	liveApply = new QCheckBox(tr("Apply changes to the source live"));
	footer->addWidget(liveApply);
	footer->addStretch();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	footer->addWidget(buttons);
	root->addLayout(footer);

	connect(liveApply, &QCheckBox::toggled, this, [this](bool on) {
		if (on)
			applyToSource();
	});
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &DesignEditor::applyToSource);
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		applyToSource();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &DesignEditor::reject);

	refreshPresetList();
}

void DesignEditor::loadFromSource()
{
	obs_data_t *settings = obs_source_get_settings(source);
	widthSpin->blockSignals(true);
	heightSpin->blockSignals(true);
	widthSpin->setValue((int)obs_data_get_int(settings, settings_keys::Width));
	heightSpin->setValue((int)obs_data_get_int(settings, settings_keys::Height));
	widthSpin->blockSignals(false);
	heightSpin->blockSignals(false);

	model->reset(resolveDesign(kind, parseObject(obs_data_get_string(settings, settings_keys::Design))));

	QJsonArray channels =
		QJsonDocument::fromJson(QByteArray(obs_data_get_string(settings, settings_keys::Channels))).array();
	channelsEditor->setChannels(channels.isEmpty() ? defaultChannels(kind) : channels);
	obs_data_release(settings);
}

obs_data_t *DesignEditor::collectSettings(bool forPreview)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, settings_keys::Width, widthSpin->value());
	obs_data_set_int(settings, settings_keys::Height, heightSpin->value());
	obs_data_set_string(settings, settings_keys::Design, toJson(model->document()).toUtf8().constData());
	obs_data_set_string(settings, settings_keys::Channels, toJson(channelsEditor->channels()).toUtf8().constData());
	obs_data_set_bool(settings, settings_keys::PreviewDemo, forPreview && demoMode->isChecked());
	return settings;
}

void DesignEditor::schedulePush()
{
	dirtySinceApply = true;
	pushTimer.start();
}

void DesignEditor::pushToPreview()
{
	if (preview) {
		obs_data_t *settings = collectSettings(true);
		obs_source_update(preview, settings);
		obs_data_release(settings);
	}
	if (liveApply->isChecked())
		applyToSource();
}

void DesignEditor::applyToSource()
{
	obs_data_t *settings = collectSettings(false);
	obs_source_update(source, settings);
	obs_data_release(settings);
	dirtySinceApply = false;
}

void DesignEditor::reject()
{
	/* Cancel undoes everything, including live-applied changes. */
	obs_source_update(source, originalSettings);
	QDialog::reject();
}

void DesignEditor::sendPreviewTest()
{
	if (auto *overlay = OverlaySource::fromSource(preview))
		overlay->sendTest();
}

/* ---- presets ---- */

QString DesignEditor::presetFolder() const
{
	QString dir = ConfigStore::instance().presetsDir() + (kind == OverlayKind::Chat ? "/chat" : "/events");
	QDir().mkpath(dir);
	return dir;
}

void DesignEditor::refreshPresetList()
{
	presetCombo->clear();
	presetCombo->addItem(tr("Choose a preset…"));
	QJsonObject themes = builtinThemes(kind);
	for (auto it = themes.begin(); it != themes.end(); ++it) {
		presetCombo->addItem(tr("Built-in: %1").arg(it.key()), it.key());
		presetCombo->setItemData(presetCombo->count() - 1, true, kBuiltinRole);
	}
	QDir dir(presetFolder());
	for (const QFileInfo &file : dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
		presetCombo->addItem(file.completeBaseName(), file.absoluteFilePath());
		presetCombo->setItemData(presetCombo->count() - 1, false, kBuiltinRole);
	}
}

void DesignEditor::applyPreset(int index)
{
	if (index <= 0)
		return;

	QJsonObject preset;
	if (presetCombo->itemData(index, kBuiltinRole).toBool()) {
		preset = builtinThemes(kind).value(presetCombo->itemData(index).toString()).toObject();
	} else {
		QFile file(presetCombo->itemData(index).toString());
		if (!file.open(QIODevice::ReadOnly))
			return;
		preset = QJsonDocument::fromJson(file.readAll()).object().value("design").toObject();
	}

	/* Presets change the look; keep this source's filters and alert media choices. */
	QJsonObject next = deepMerge(defaultDesign(kind), preset);
	QJsonObject current = model->document();
	if (kind == OverlayKind::Chat) {
		next["filters"] = current.value("filters");
	} else if (!preset.contains("types")) {
		next["types"] = current.value("types");
	}
	model->reset(next);
}

void DesignEditor::savePreset()
{
	bool ok = false;
	QString name =
		QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"), QLineEdit::Normal, QString(), &ok)
			.trimmed();
	if (!ok || name.isEmpty())
		return;
	name.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

	QFile file(presetFolder() + "/" + name + ".json");
	if (file.exists() && QMessageBox::question(this, tr("Save preset"), tr("Overwrite preset \"%1\"?").arg(name)) !=
				     QMessageBox::Yes)
		return;
	if (!file.open(QIODevice::WriteOnly)) {
		QMessageBox::warning(this, tr("Save preset"), tr("Could not write %1").arg(file.fileName()));
		return;
	}
	QJsonObject doc{{"kind", kind == OverlayKind::Chat ? "chat" : "events"}, {"design", model->document()}};
	file.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
	file.close();
	refreshPresetList();
	presetCombo->setCurrentIndex(presetCombo->findText(name));
}

void DesignEditor::deletePreset()
{
	int index = presetCombo->currentIndex();
	if (index <= 0 || presetCombo->itemData(index, kBuiltinRole).toBool())
		return;
	if (QMessageBox::question(this, tr("Delete preset"),
				  tr("Delete preset \"%1\"?").arg(presetCombo->currentText())) != QMessageBox::Yes)
		return;
	QFile::remove(presetCombo->itemData(index).toString());
	refreshPresetList();
}

void DesignEditor::importPreset()
{
	QString path = QFileDialog::getOpenFileName(this, tr("Import preset"), QString(), tr("Preset (*.json)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;
	QJsonObject doc = QJsonDocument::fromJson(file.readAll()).object();
	QString expected = kind == OverlayKind::Chat ? "chat" : "events";
	if (doc.value("kind").toString() != expected || !doc.value("design").isObject()) {
		QMessageBox::warning(this, tr("Import preset"),
				     tr("This file is not a %1 preset.")
					     .arg(kind == OverlayKind::Chat ? tr("Chat Feed") : tr("Event Display")));
		return;
	}
	QFile::copy(path, presetFolder() + "/" + QFileInfo(path).fileName());
	model->reset(resolveDesign(kind, doc.value("design").toObject()));
	refreshPresetList();
}

void DesignEditor::exportPreset()
{
	QString path = QFileDialog::getSaveFileName(this, tr("Export preset"), "social-feed-preset.json",
						    tr("Preset (*.json)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		QMessageBox::warning(this, tr("Export preset"), tr("Could not write %1").arg(path));
		return;
	}
	QJsonObject doc{{"kind", kind == OverlayKind::Chat ? "chat" : "events"}, {"design", model->document()}};
	file.write(QJsonDocument(doc).toJson(QJsonDocument::Indented));
}

void DesignEditor::resetDesign()
{
	if (QMessageBox::question(this, tr("Reset design"), tr("Reset every design option to its default?")) !=
	    QMessageBox::Yes)
		return;
	model->reset(defaultDesign(kind));
}

} // namespace sf::ui
