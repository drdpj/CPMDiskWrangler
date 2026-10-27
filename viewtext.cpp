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
#include "viewtext.h"

namespace ViewText {

QString cpmText(const QByteArray &d, bool stopAtEof, bool stripBit7, int *eofAt) {
    QString s;
    s.reserve(d.size());
    if (eofAt) *eofAt = d.indexOf(char(0x1A));
    for (int i = 0; i < d.size(); ++i) {
        quint8 c = quint8(d[i]);
        if (stripBit7) c &= 0x7F;
        if (c == 0x1A && stopAtEof) break;
        if (c == 0x0D) { s += '\n'; if (i + 1 < d.size() && (quint8(d[i + 1]) & (stripBit7 ? 0x7F : 0xFF)) == 0x0A) ++i; }
        else if (c == 0x0A || c == 0x09) s += QChar(c);
        else if (c >= 0x20 && c < 0x7F) s += QChar(c);
        else if (c >= 0xA0) s += QChar(c);
        else s += QChar('.');
    }
    return s;
}

QString hexDump(const QByteArray &d) {
    QString out;
    out.reserve(d.size() * 4 + 64);
    for (int o = 0; o < d.size(); o += 16) {
        QString hex, asc;
        for (int i = 0; i < 16; ++i) {
            if (o + i < d.size()) {
                quint8 c = quint8(d[o + i]);
                hex += QString("%1 ").arg(c, 2, 16, QChar('0')).toUpper();
                asc += (c >= 0x20 && c < 0x7F) ? QChar(c) : QChar('.');
            } else hex += "   ";
            if (i == 7) hex += ' ';
        }
        out += QString("%1  %2 |%3|\n").arg(o, 8, 16, QChar('0')).toUpper().arg(hex, asc);
    }
    return out;
}

static double printableShare(const QByteArray &d, bool strip) {
    int end = d.indexOf(char(0x1A));
    if (end < 0) end = d.size();
    if (end == 0) return d.isEmpty() ? 1.0 : 0.0;
    int n = 0;
    for (int i = 0; i < end; ++i) {
        quint8 c = quint8(d[i]);
        if (c == 0) return 0;
        if (strip) c &= 0x7F;
        if ((c >= 0x20 && c < 0x7F) || c == '\r' || c == '\n' || c == '\t' || c == 0x0C) ++n;
    }
    return double(n) / end;
}

bool looksLikeText(const QByteArray &d) { return printableShare(d.left(8192), false) >= 0.95 || looksLikeWordStar(d); }

bool looksLikeWordStar(const QByteArray &d) {
    QByteArray s = d.left(8192);
    int high = 0;
    for (char c : s) if (quint8(c) & 0x80) ++high;
    return high > s.size() / 50 && printableShare(s, true) >= 0.95;
}

} // namespace ViewText
