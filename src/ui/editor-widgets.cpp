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

#include "ui/editor-widgets.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

namespace sf::ui {

namespace {

QJsonObject setIn(QJsonObject object, const QStringList &keys, int index, const QJsonValue &value)
{
	const QString &key = keys[index];
	if (index == keys.size() - 1) {
		object[key] = value;
		return object;
	}
	object[key] = setIn(object.value(key).toObject(), keys, index + 1, value);
	return object;
}

} // namespace

void DesignModel::reset(const QJsonObject &document)
{
	doc = document;
	emit reloaded();
	emit changed();
}

QJsonValue DesignModel::value(const QString &path) const
{
	QJsonValue current = doc;
	for (const QString &key : path.split('.'))
		current = current.toObject().value(key);
	return current;
}

void DesignModel::setValue(const QString &path, const QJsonValue &value)
{
	if (this->value(path) == value)
		return;
	doc = setIn(doc, path.split('.'), 0, value);
	emit changed();
}

/* ---- ColorButton ---- */

ColorButton::ColorButton(QWidget *parent) : QPushButton(parent)
{
	setMinimumWidth(90);
	connect(this, &QPushButton::clicked, this, &ColorButton::pick);
}

QColor ColorButton::parse(const QString &color)
{
	QString c = color.trimmed();
	if (c.startsWith('#') && c.size() == 9) {
		/* CSS order is RRGGBBAA; QColor expects AARRGGBB. */
		QColor rgb(c.left(7));
		rgb.setAlpha(c.mid(7, 2).toInt(nullptr, 16));
		return rgb;
	}
	return QColor(c);
}

QString ColorButton::format(const QColor &color)
{
	return QStringLiteral("#%1%2%3%4")
		.arg(color.red(), 2, 16, QChar('0'))
		.arg(color.green(), 2, 16, QChar('0'))
		.arg(color.blue(), 2, 16, QChar('0'))
		.arg(color.alpha(), 2, 16, QChar('0'))
		.toUpper();
}

void ColorButton::setColorString(const QString &color)
{
	value = color;
	QColor c = parse(color);
	QColor opaque = c;
	opaque.setAlpha(255);
	QString textColor = c.lightness() > 140 || c.alpha() < 100 ? "#000000" : "#FFFFFF";
	setText(value);
	setStyleSheet(QStringLiteral("QPushButton { background-color: rgba(%1,%2,%3,%4); color: %5; "
				     "border: 1px solid #666; padding: 3px 8px; }")
			      .arg(c.red())
			      .arg(c.green())
			      .arg(c.blue())
			      .arg(c.alpha())
			      .arg(textColor));
}

void ColorButton::pick()
{
	QColor chosen = QColorDialog::getColor(parse(value), this, tr("Choose color"), QColorDialog::ShowAlphaChannel);
	if (!chosen.isValid())
		return;
	setColorString(format(chosen));
	emit colorChanged(value);
}

/* ---- FilePicker ---- */

FilePicker::FilePicker(const QString &filter, QWidget *parent) : QWidget(parent), filter(filter)
{
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	edit = new QLineEdit(this);
	edit->setPlaceholderText(tr("Local file or https:// URL"));
	auto *browse = new QToolButton(this);
	browse->setText(QStringLiteral("…"));
	auto *clear = new QToolButton(this);
	clear->setText(QStringLiteral("✕"));
	layout->addWidget(edit, 1);
	layout->addWidget(browse);
	layout->addWidget(clear);

	connect(edit, &QLineEdit::editingFinished, this, [this]() { emit pathChanged(edit->text()); });
	connect(browse, &QToolButton::clicked, this, [this]() {
		QString file = QFileDialog::getOpenFileName(this, tr("Choose file"), edit->text(), this->filter);
		if (!file.isEmpty()) {
			edit->setText(file);
			emit pathChanged(file);
		}
	});
	connect(clear, &QToolButton::clicked, this, [this]() {
		edit->clear();
		emit pathChanged(QString());
	});
}

QString FilePicker::path() const
{
	return edit->text();
}

void FilePicker::setPath(const QString &path)
{
	edit->setText(path);
}

/* ---- binding helpers ---- */

namespace {

template<typename Refresh> void onReload(DesignModel *model, QWidget *widget, Refresh refresh)
{
	refresh();
	QObject::connect(model, &DesignModel::reloaded, widget, refresh);
}

} // namespace

QWidget *bindSpin(QFormLayout *form, DesignModel *model, const QString &label, const QString &path, int min, int max,
		  const QString &suffix)
{
	auto *spin = new QSpinBox();
	spin->setRange(min, max);
	spin->setSuffix(suffix);
	onReload(model, spin, [=]() {
		QSignalBlocker block(spin);
		spin->setValue(model->value(path).toInt());
	});
	QObject::connect(spin, &QSpinBox::valueChanged, model, [=](int v) { model->setValue(path, v); });
	form->addRow(label, spin);
	return spin;
}

QWidget *bindDouble(QFormLayout *form, DesignModel *model, const QString &label, const QString &path, double min,
		    double max, double step, const QString &suffix)
{
	auto *spin = new QDoubleSpinBox();
	spin->setRange(min, max);
	spin->setSingleStep(step);
	spin->setDecimals(2);
	spin->setSuffix(suffix);
	onReload(model, spin, [=]() {
		QSignalBlocker block(spin);
		spin->setValue(model->value(path).toDouble());
	});
	QObject::connect(spin, &QDoubleSpinBox::valueChanged, model, [=](double v) { model->setValue(path, v); });
	form->addRow(label, spin);
	return spin;
}

QWidget *bindCheck(QFormLayout *form, DesignModel *model, const QString &label, const QString &path)
{
	auto *check = new QCheckBox(label);
	onReload(model, check, [=]() {
		QSignalBlocker block(check);
		check->setChecked(model->value(path).toBool());
	});
	QObject::connect(check, &QCheckBox::toggled, model, [=](bool v) { model->setValue(path, v); });
	form->addRow(QString(), check);
	return check;
}

QWidget *bindCombo(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		   const Options &options)
{
	auto *combo = new QComboBox();
	for (const auto &[value, text] : options)
		combo->addItem(text, value);
	onReload(model, combo, [=]() {
		QSignalBlocker block(combo);
		int index = combo->findData(model->value(path).toString());
		combo->setCurrentIndex(std::max(0, index));
	});
	QObject::connect(combo, &QComboBox::currentIndexChanged, model,
			 [=](int) { model->setValue(path, combo->currentData().toString()); });
	form->addRow(label, combo);
	return combo;
}

QWidget *bindColor(QFormLayout *form, DesignModel *model, const QString &label, const QString &path)
{
	auto *button = new ColorButton();
	onReload(model, button, [=]() { button->setColorString(model->value(path).toString()); });
	QObject::connect(button, &ColorButton::colorChanged, model,
			 [=](const QString &c) { model->setValue(path, c); });
	form->addRow(label, button);
	return button;
}

QWidget *bindText(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		  const QString &placeholder)
{
	auto *edit = new QLineEdit();
	edit->setPlaceholderText(placeholder);
	onReload(model, edit, [=]() {
		QSignalBlocker block(edit);
		edit->setText(model->value(path).toString());
	});
	QObject::connect(edit, &QLineEdit::textEdited, model, [=](const QString &t) { model->setValue(path, t); });
	form->addRow(label, edit);
	return edit;
}

QWidget *bindFont(QFormLayout *form, DesignModel *model, const QString &label, const QString &path)
{
	/* Editable so CSS font stacks and web fonts can be typed in as well. */
	auto *combo = new QFontComboBox();
	combo->setEditable(true);
	onReload(model, combo, [=]() {
		QSignalBlocker block(combo);
		combo->setEditText(model->value(path).toString());
	});
	QObject::connect(combo, &QFontComboBox::currentTextChanged, model,
			 [=](const QString &t) { model->setValue(path, t); });
	form->addRow(label, combo);
	return combo;
}

QWidget *bindList(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		  const QString &placeholder)
{
	auto *edit = new QLineEdit();
	edit->setPlaceholderText(placeholder.isEmpty() ? QObject::tr("comma, separated, values") : placeholder);
	onReload(model, edit, [=]() {
		QSignalBlocker block(edit);
		QStringList items;
		for (const QJsonValue v : model->value(path).toArray())
			items.append(v.toString());
		edit->setText(items.join(", "));
	});
	QObject::connect(edit, &QLineEdit::textEdited, model, [=](const QString &text) {
		QJsonArray array;
		for (const QString &item : text.split(',', Qt::SkipEmptyParts)) {
			QString trimmed = item.trimmed();
			if (!trimmed.isEmpty())
				array.append(trimmed);
		}
		model->setValue(path, array);
	});
	form->addRow(label, edit);
	return edit;
}

QWidget *bindFile(QFormLayout *form, DesignModel *model, const QString &label, const QString &path,
		  const QString &filter)
{
	auto *picker = new FilePicker(filter);
	onReload(model, picker, [=]() { picker->setPath(model->value(path).toString()); });
	QObject::connect(picker, &FilePicker::pathChanged, model, [=](const QString &p) { model->setValue(path, p); });
	form->addRow(label, picker);
	return picker;
}

const Options &animationInOptions()
{
	static const Options options = {
		{"none", QObject::tr("None")},
		{"fade", QObject::tr("Fade in")},
		{"slide-left", QObject::tr("Slide in from left")},
		{"slide-right", QObject::tr("Slide in from right")},
		{"slide-up", QObject::tr("Slide in from below")},
		{"slide-down", QObject::tr("Slide in from above")},
		{"zoom", QObject::tr("Zoom in")},
		{"pop", QObject::tr("Pop")},
		{"bounce", QObject::tr("Bounce")},
		{"flip", QObject::tr("Flip")},
		{"blur", QObject::tr("Blur in")},
	};
	return options;
}

const Options &animationOutOptions()
{
	static const Options options = {
		{"none", QObject::tr("None")},
		{"fade", QObject::tr("Fade out")},
		{"slide-left", QObject::tr("Slide out to left")},
		{"slide-right", QObject::tr("Slide out to right")},
		{"slide-up", QObject::tr("Slide out upwards")},
		{"slide-down", QObject::tr("Slide out downwards")},
		{"zoom", QObject::tr("Zoom out")},
		{"shrink", QObject::tr("Shrink")},
		{"flip", QObject::tr("Flip")},
		{"blur", QObject::tr("Blur out")},
	};
	return options;
}

const Options &easingOptions()
{
	static const Options options = {
		{"linear", QObject::tr("Linear")},
		{"ease", QObject::tr("Ease")},
		{"ease-in", QObject::tr("Ease in")},
		{"ease-out", QObject::tr("Ease out")},
		{"ease-in-out", QObject::tr("Ease in-out")},
		{"cubic-bezier(0.34, 1.56, 0.64, 1)", QObject::tr("Overshoot")},
		{"cubic-bezier(0.68, -0.6, 0.32, 1.6)", QObject::tr("Back in-out")},
	};
	return options;
}

QWidget *makeFormPage(QFormLayout *&form)
{
	auto *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget();
	form = new QFormLayout(content);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	scroll->setWidget(content);
	return scroll;
}

void addSection(QFormLayout *form, const QString &title)
{
	auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
	label->setContentsMargins(0, form->rowCount() > 0 ? 10 : 0, 0, 2);
	form->addRow(label);
}

} // namespace sf::ui
