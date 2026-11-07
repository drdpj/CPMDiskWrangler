/*
 * CP/M Disk Wrangler - view and edit CP/M disc images (raw, ImageDisk, CPCEMU DSK, TeleDisk)
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// diskimage.h - forwarding names for the tests only. The physical disc (containers, staging, saving) is
// DiskWranglerCore's dw::device::Container (core/diskwrangler/device/container.h); the program's own code uses
// those names. The tests keep Disk::Image and friends until they are ported to the core's API (after step 5 of the
// DiskWranglerCore migration).
#pragma once

#include <diskwrangler/device/container.h>

namespace Disk {
using Image = dw::device::Container;
using dw::device::Kind;
using dw::device::kindName;
using dw::device::kindExtension;
using dw::device::Sector;
using dw::device::Track;
using dw::device::RawLayout;
using dw::device::Shape;
using dw::device::lzhufDecode;
} // namespace Disk
