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
// mkicon <dir>: writes the application icon as cpmdiskwrangler-<size>.png for the usual sizes.
#include <QGuiApplication>
#include "../icon.h"
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QString dir = argc > 1 ? argv[1] : ".";
    for (int s : {16, 24, 32, 48, 64, 128, 256, 512}) floppyIconImage(s).save(QString("%1/cpmdiskwrangler-%2.png").arg(dir).arg(s));
    return 0;
}
