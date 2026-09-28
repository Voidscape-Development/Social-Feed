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

#include "providers/provider.hpp"

#include <memory>
#include <vector>

namespace sf {

/* Placeholders for platforms that are planned but not implemented yet (Kick, TikTok,
 * StreamElements, Streamlabs, Streamer.bot). They register so the UI, channel lists and event
 * type settings already cover them; they report "Coming soon". */
std::vector<std::unique_ptr<Provider>> makeStubProviders();

} // namespace sf
