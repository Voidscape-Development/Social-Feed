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

#include <obs.h>

#include <QWidget>

#include <atomic>

namespace sf::ui {

/* Renders an OBS source into a native child window via obs_display, letterboxed to fit. */
class PreviewWidget : public QWidget {
	Q_OBJECT

public:
	enum class Background { Dark, Light, Grey };

	explicit PreviewWidget(QWidget *parent = nullptr);
	~PreviewWidget() override;

	/* Holds a weak reference; pass nullptr to clear. */
	void setSource(obs_source_t *source);
	void setBackground(Background background);

	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	bool event(QEvent *event) override;

private:
	void createDisplay();
	void destroyDisplay();
	static void draw(void *data, uint32_t cx, uint32_t cy);

	obs_display_t *display = nullptr;
	obs_weak_source_t *weakSource = nullptr;
	std::atomic<int> background{0};
};

} // namespace sf::ui
