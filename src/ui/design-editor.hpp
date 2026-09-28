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

#include "sources/design-defaults.hpp"

#include <obs.h>

#include <QDialog>
#include <QJsonArray>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace sf::ui {

class DesignModel;
class PreviewWidget;

/* Opens (or raises) the pop-out design editor for a Chat Feed / Event Display source. */
void openDesignEditor(obs_source_t *source);
void closeAllEditors();

/* Table of {platform, channel} rows; an empty channel means "my connected account". */
class ChannelsEditor : public QWidget {
	Q_OBJECT

public:
	ChannelsEditor(OverlayKind kind, QWidget *parent = nullptr);

	QJsonArray channels() const;
	void setChannels(const QJsonArray &channels);

signals:
	void changed();

private:
	void addRow(const QString &platform, const QString &channel);

	OverlayKind kind;
	QTableWidget *table;
};

class DesignEditor : public QDialog {
	Q_OBJECT

public:
	DesignEditor(obs_source_t *source, QWidget *parent = nullptr);
	~DesignEditor() override;

	void sendPreviewTest();

protected:
	void reject() override;

private:
	void buildUi();
	void loadFromSource();
	void schedulePush();
	void pushToPreview();
	void applyToSource();
	obs_data_t *collectSettings(bool forPreview);

	void refreshPresetList();
	void applyPreset(int index);
	void savePreset();
	void deletePreset();
	void importPreset();
	void exportPreset();
	void resetDesign();
	QString presetFolder() const;

	static void sourceRemoved(void *data, calldata_t *cd);

	OverlayKind kind;
	obs_source_t *source;
	obs_source_t *preview = nullptr;
	obs_data_t *originalSettings = nullptr;

	DesignModel *model;
	ChannelsEditor *channelsEditor = nullptr;
	PreviewWidget *previewWidget = nullptr;
	QSpinBox *widthSpin = nullptr;
	QSpinBox *heightSpin = nullptr;
	QComboBox *presetCombo = nullptr;
	QCheckBox *liveApply = nullptr;
	QCheckBox *demoMode = nullptr;
	QTimer pushTimer;
	bool dirtySinceApply = false;
};

} // namespace sf::ui
