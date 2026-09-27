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

#include <QColor>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPushButton>
#include <QString>
#include <QWidget>

class QFormLayout;
class QLineEdit;

namespace sf::ui {

/* The design document being edited. Paths are dot separated ("text.fontSize"). */
class DesignModel : public QObject {
	Q_OBJECT

public:
	explicit DesignModel(QObject *parent = nullptr) : QObject(parent) {}

	const QJsonObject &document() const { return doc; }
	/* Replaces the whole document and refreshes every bound control. */
	void reset(const QJsonObject &document);

	QJsonValue value(const QString &path) const;
	void setValue(const QString &path, const QJsonValue &value);

signals:
	void changed();
	void reloaded();

private:
	QJsonObject doc;
};

/* Swatch button that edits an #RRGGBBAA color. */
class ColorButton : public QPushButton {
	Q_OBJECT

public:
	explicit ColorButton(QWidget *parent = nullptr);

	QString colorString() const { return value; }
	void setColorString(const QString &color);

	static QColor parse(const QString &color);
	static QString format(const QColor &color);

signals:
	void colorChanged(const QString &color);

private:
	void pick();
	QString value;
};

/* Line edit with a browse button for media files. */
class FilePicker : public QWidget {
	Q_OBJECT

public:
	FilePicker(const QString &filter, QWidget *parent = nullptr);

	QString path() const;
	void setPath(const QString &path);

signals:
	void pathChanged(const QString &path);

private:
	QLineEdit *edit;
	QString filter;
};

using Options = QList<QPair<QString, QString>>; /* value, label */

/* Factory helpers: each adds a labelled control to `form`, initialised from and writing to
 * `path` in `model`, and re-reads the model when it is reloaded (preset load, reset). */
QWidget *bindSpin(QFormLayout *form, DesignModel *model, const QString &label, const QString &path, int min, int max,
		  const QString &suffix = QString());
QWidget *bindDouble(QFormLayout *form, DesignModel *model, const QString &label, const QString &path, double min,
		    double max, double step, const QString &suffix = QString());
QWidget *bindCheck(QFormLayout *form, DesignModel *model, const QString &label, const QString &path);
QWidget *bindCombo(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		   const Options &options);
QWidget *bindColor(QFormLayout *form, DesignModel *model, const QString &label, const QString &path);
QWidget *bindText(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		  const QString &placeholder = QString());
QWidget *bindFont(QFormLayout *form, DesignModel *model, const QString &label, const QString &path);
/* Comma separated list <-> JSON string array. */
QWidget *bindList(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		  const QString &placeholder = QString());
QWidget *bindFile(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		  const QString &filter);

const Options &animationInOptions();
const Options &animationOutOptions();
const Options &easingOptions();

/* Scrollable page holding a form layout, for editor tabs. */
QWidget *makeFormPage(QFormLayout *&form);
void addSection(QFormLayout *form, const QString &title);

} // namespace sf::ui
