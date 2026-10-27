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
// icon.h - the application icon: an 8-inch floppy disc (Acorn Disk Wrangler's is a 5.25-inch one), drawn with
// QPainter so it is sharp at every size. icons/ holds PNG exports (tests/mkicon <dir>) and a matching SVG.
#pragma once
#include <QIcon>
#include <QImage>

QImage floppyIconImage(int size);
QIcon appIcon();
