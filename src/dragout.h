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
// dragout.h - dragging files out of a disc to the host or to another program, and the "retro-files" drag format
// (docs/retro-files-format.md, shared with Acorn Disk Wrangler) in both directions. The format itself - the document,
// the path checks, the drag data - is DiskWranglerCore's (diskwrangler/xfer, diskwrangler/ui/dragoutmime.h); this
// file turns holds what is CP/M's: the "cpm"
// attributes, a folder per user, the ^Z padding trim and the host names of CP/M files.
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QMimeData>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include <diskwrangler/ui/dragoutmime.h>
#include <diskwrangler/xfer/transferitem.h>

#include "hostpolicy.h"

namespace RetroFiles {

extern const char *const MimeType;          // "application/x-retro-files+json"
extern const char *const Generator;         // "CP/M Disk Wrangler"

// The CP/M ^Z (0x1A) padding after the end of a text file: everything from the first ^Z in the last record on,
// if the rest of the record is ^Z too. Returns the data unchanged if it doesn't look like padded text.
// trimCpmText (the ^Z padding trim) and CpmPolicy are in hostpolicy.h.

QString dragDirPrefix();
int cleanStale(qint64 maxAgeSecs = 24 * 3600);

typedef dw::DragState DragState;

// How this program's drags (and Edit > Copy) are written out: the core's DragSpec with CP/M's policy (no folder per
// user), "cpm" as the source filesystem and this program's drag folders. The window builds a dw::DragOutMime from
// it and an exporter of TransferItems (MainWindow::exportForCopy).
dw::DragSpec dragSpec(bool trimText, const QString &imageName);

} // namespace RetroFiles
