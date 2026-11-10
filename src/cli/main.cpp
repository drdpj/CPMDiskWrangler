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
// main.cpp - cpmdw, the command-line program: the same discs and the same checked saving as CP/M Disk Wrangler, without a window.
#include <QCoreApplication>

#include "backend.h"
#include "../cpmvolume.h"

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    CpmCli::Backend backend;
    dw::cli::AppInfo info;
    info.tool = "cpmdw";
    info.program = Cpm::appInfo(APP_VERSION);
    info.backend = &backend;
    info.options = QStringList{"diskdefs", "charset", "user-folders", "trim", "stamps", "label", "separate-sides", "format-open"};
    return dw::runCli(argc, argv, info);
}
