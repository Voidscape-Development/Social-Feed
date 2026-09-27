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

#include "ui/preview-widget.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWindow>

#include <algorithm>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#if __has_include(<qpa/qplatformnativeinterface.h>)
#include <qpa/qplatformnativeinterface.h>
#define SF_HAVE_QPA 1
#endif
#endif

namespace sf::ui {

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumSize(240, 160);
}

PreviewWidget::~PreviewWidget()
{
	destroyDisplay();
	setSource(nullptr);
}

void PreviewWidget::setSource(obs_source_t *source)
{
	obs_weak_source_release(weakSource);
	weakSource = source ? obs_source_get_weak_source(source) : nullptr;
}

void PreviewWidget::setBackground(Background bg)
{
	background = (int)bg;
	if (display) {
		static const uint32_t colors[] = {0x1E1E24, 0xF0F0F0, 0x5A5A60};
		obs_display_set_background_color(display, colors[(int)bg]);
	}
}

void PreviewWidget::createDisplay()
{
	if (display || !windowHandle() || !isVisible())
		return;

	QSize size = this->size() * devicePixelRatioF();

	gs_init_data info = {};
	info.cx = (uint32_t)size.width();
	info.cy = (uint32_t)size.height();
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

#ifdef _WIN32
	info.window.hwnd = (void *)winId();
#elif defined(__APPLE__)
	info.window.view = (id)winId();
#else
	switch (obs_get_nix_platform()) {
	case OBS_NIX_PLATFORM_X11_EGL:
		info.window.id = (uint32_t)winId();
		info.window.display = obs_get_nix_platform_display();
		break;
#ifdef SF_HAVE_QPA
	case OBS_NIX_PLATFORM_WAYLAND: {
		QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
		info.window.display = native->nativeResourceForWindow("surface", windowHandle());
		break;
	}
#endif
	default:
		return;
	}
#endif

	display = obs_display_create(&info, 0x1E1E24);
	if (!display)
		return;
	setBackground((Background)background.load());
	obs_display_add_draw_callback(display, draw, this);
}

void PreviewWidget::destroyDisplay()
{
	if (!display)
		return;
	obs_display_remove_draw_callback(display, draw, this);
	obs_display_destroy(display);
	display = nullptr;
}

void PreviewWidget::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	createDisplay();
}

void PreviewWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	createDisplay();
	if (display) {
		QSize size = event->size() * devicePixelRatioF();
		obs_display_resize(display, (uint32_t)size.width(), (uint32_t)size.height());
	}
}

void PreviewWidget::paintEvent(QPaintEvent *event)
{
	createDisplay();
	QWidget::paintEvent(event);
}

bool PreviewWidget::event(QEvent *event)
{
	/* The native surface is recreated when the widget moves between windows/screens. */
	if (event->type() == QEvent::PlatformSurface || event->type() == QEvent::WinIdChange)
		destroyDisplay();
	return QWidget::event(event);
}

void PreviewWidget::draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *widget = static_cast<PreviewWidget *>(data);
	obs_source_t *source = obs_weak_source_get_source(widget->weakSource);
	if (!source)
		return;

	uint32_t sourceCx = obs_source_get_width(source);
	uint32_t sourceCy = obs_source_get_height(source);
	if (sourceCx && sourceCy && cx && cy) {
		float scale = std::min((float)cx / sourceCx, (float)cy / sourceCy);
		int w = (int)(sourceCx * scale);
		int h = (int)(sourceCy * scale);
		int x = ((int)cx - w) / 2;
		int y = ((int)cy - h) / 2;

		gs_viewport_push();
		gs_projection_push();
		gs_ortho(0.0f, (float)sourceCx, 0.0f, (float)sourceCy, -100.0f, 100.0f);
		gs_set_viewport(x, y, w, h);
		obs_source_video_render(source);
		gs_projection_pop();
		gs_viewport_pop();
	}
	obs_source_release(source);
}

} // namespace sf::ui
