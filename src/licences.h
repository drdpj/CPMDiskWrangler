/*
 * Licence viewer - shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#pragma once
class QWidget;

// Help > Licences: shows every licence text compiled into the program, so a copy of the binary on its own
// still carries them. They are resources under ":/licences/":
//   licences.qrc          (in the repository)  this program's LICENSE, THIRD-PARTY-NOTICES.md, the LGPL v3 for Qt
//   licences-bundled.qrc  (made by ci/collect-licences.sh for a release build, not in the repository)
//                         "Bundled/..." - the licences of every library shipped alongside the program
void showLicences(QWidget *parent);
