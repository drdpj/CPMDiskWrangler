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
// viewtext.h - turn file bytes into readable text: CP/M text (up to the ^Z end-of-file mark, WordStar's high bits
// optionally stripped) and a hex dump.
#pragma once
#include <QByteArray>
#include <QString>

namespace ViewText {

// CR / LF / CRLF become newlines, tabs are kept, other control characters are shown as '.'. With stopAtEof the
// text ends at the first ^Z (*eofAt is set to its offset, or -1). With stripBit7, bit 7 is ignored (WordStar).
QString cpmText(const QByteArray &d, bool stopAtEof, bool stripBit7, int *eofAt = nullptr);
// "00000000  41 42 ...  |AB..|" 16 bytes per line.
QString hexDump(const QByteArray &d);
// Mostly printable up to the first ^Z, no NULs.
bool looksLikeText(const QByteArray &d);
// Many characters with bit 7 set among otherwise printable text: probably a WordStar document.
bool looksLikeWordStar(const QByteArray &d);

} // namespace ViewText
