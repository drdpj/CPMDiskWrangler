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
// natsets.h - national character sets, the QtCore part: the table of sets and the conversions between the bytes on the
// disc and the characters of the host (natchars.h has the dialog and the scanning that guesses a set). CP/M names and
// texts are 7-bit, and on machines sold in many countries the ASCII positions @ [ \ ] ^ ` { | } ~ held that country's
// letters instead (the ISO 646 national variants): Danish "brøkregning" is written br|kregning. Nothing on the disc says
// which set it uses; it only changes how names and text are shown, extracted, added and viewed, never the bytes on the
// disc. # and $ are never mapped (real names use them far more often than £ or ¤).
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Cpm { class Store; }

namespace Nat {

struct Set {
    QString id;            // "ascii", "dk-no", ... (kept in the settings)
    QString name;          // "Danish / Norwegian"
    QString shown;         // what each of positions() shows, in order
};

const QVector<Set> &sets();                 // [0] is "ascii": as on the disc
const Set &find(const QString &id);         // ascii if the id is unknown
QString positions();                        // "@[\]^`{|}~"

// Disc bytes (as a Latin-1 string) -> what to show or give the host, and back. fromHost upper-cases (CP/M names are
// upper case; ß is kept) unless upper is false. Characters the set has no position for are left as they are.
QString toHost(const QString &s, const Set &set);
QString fromHost(const QString &s, const Set &set, bool upper = true);
// Name characters a set needs that Volume's normal list does not allow ("[\]|"); empty for ascii.
QString extraNameChars(const Set &set);
// Some discs (COMAL's, for one) hold lower-case names. CP/M tools upper-case what you type, so a disc with any lower-case
// name is offered "Allow lower-case file names" (ticked), and names then go back exactly as they came out.
QString lowerLetters();                                          // "a".."z": allowed in names on such a disc
bool usesLowerCase(const QVector<const Cpm::Store *> &vols);

} // namespace Nat
