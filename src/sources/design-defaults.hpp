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

#include <QJsonArray>
#include <QJsonObject>

namespace sf {

enum class OverlayKind { Chat, Events };

/* Default design documents. A stored design only contains what the user changed from these,
 * or a full copy; either way it is deep-merged over the defaults before use, so new options
 * added in later versions pick up their defaults automatically. The schema is documented in
 * docs/design-schema.md. */
QJsonObject defaultDesign(OverlayKind kind);
QJsonArray defaultChannels(OverlayKind kind);

/* Recursively overlays `overrides` onto `base` (objects merge, everything else replaces). */
QJsonObject deepMerge(const QJsonObject &base, const QJsonObject &overrides);

QJsonObject resolveDesign(OverlayKind kind, const QJsonObject &stored);

/* Named built-in themes offered in the editor's preset list. */
QJsonObject builtinThemes(OverlayKind kind);

} // namespace sf
