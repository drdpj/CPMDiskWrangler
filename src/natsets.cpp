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
#include "natsets.h"
#include "cpmfs.h"

namespace Nat {

QString positions() { return QStringLiteral("@[\\]^`{|}~"); }

const QVector<Set> &sets() {
    // Each "shown" has one character for each of positions(): @ [ \ ] ^ ` { | } ~. The commonest national sets only:
    // Danish/Norwegian (ISO 646-DK/NO), Swedish/Finnish (SEN 850200 B), German (DIN 66003), French (NF Z 62-010),
    // Italian (UNI 0204-70), Spanish (UNE 5), Portuguese (ISO 646-PT). Where a set also moves ^ or ` (Swedish C, the
    // DEC Norwegian/Danish set) those positions are left alone: they are in ordinary names and text far more often.
    static const QVector<Set> s = {
        {"ascii", QStringLiteral("As on the disc (no national letters)"), QStringLiteral("@[\\]^`{|}~")},
        {"dk-no", QStringLiteral("Danish / Norwegian  (Æ Ø Å  æ ø å)"),
                  QStringLiteral("@ÆØÅ^`æøå~")},
        {"se-fi", QStringLiteral("Swedish / Finnish  (Ä Ö Å  ä ö å)"),
                  QStringLiteral("@ÄÖÅ^`äöå~")},
        {"de",    QStringLiteral("German  (Ä Ö Ü  ä ö ü  ß)"),
                  QStringLiteral("§ÄÖÜ^`äöüß")},
        {"fr",    QStringLiteral("French  (à ç é è ù)"),
                  QStringLiteral("à°ç§^`éùè¨")},
        {"it",    QStringLiteral("Italian  (à ç é è ò ù ì)"),
                  QStringLiteral("§°çé^ùàòèì")},
        {"es",    QStringLiteral("Spanish  (Ñ ñ ç  ¡ ¿)"),
                  QStringLiteral("§¡Ñ¿^`°ñç~")},
        {"pt",    QStringLiteral("Portuguese  (Ã Ç Õ  ã ç õ)"),
                  QStringLiteral("@ÃÇÕ^`ãçõ~")},
    };
    return s;
}

const Set &find(const QString &id) {
    for (const Set &x : sets()) if (x.id == id) return x;
    return sets().first();
}

QString toHost(const QString &s, const Set &set) {
    if (set.id == "ascii") return s;
    const QString pos = positions();
    QString o = s;
    for (QChar &c : o) {
        int i = pos.indexOf(c);
        if (i >= 0) c = set.shown.at(i);
    }
    return o;
}

QString fromHost(const QString &s, const Set &set, bool upper) {
    if (set.id == "ascii") return upper ? s.toUpper() : s;
    const QString pos = positions();
    QString o;
    for (QChar c : s) {
        QChar u = (upper && c != QChar(0x00DF)) ? c.toUpper() : c;     // ß stays: its capital is "SS"
        int i = set.shown.indexOf(u);
        if (i < 0 && upper) i = set.shown.indexOf(c);                  // a letter the set has only in lower case (fr ç)
        if (i >= 0 && set.shown.at(i) == pos.at(i)) i = -1;
        o += i >= 0 ? pos.at(i) : u;
    }
    return o;
}

QString lowerLetters() { return QStringLiteral("abcdefghijklmnopqrstuvwxyz"); }

bool usesLowerCase(const QVector<const Cpm::Volume *> &vols) {
    QVector<const Cpm::Store *> s;
    for (const Cpm::Volume *v : vols) s << v;
    return usesLowerCase(s);
}

bool usesLowerCase(const QVector<const Cpm::Store *> &vols) {
    int total = 0, lower = 0;
    for (const Cpm::Store *v : vols) {
        if (!v) continue;
        for (const Cpm::File &f : v->files()) {
            ++total;
            for (QChar c : f.name) if (c >= 'a' && c <= 'z') { ++lower; break; }
        }
    }
    return total > 0 && lower > 0;
}

QString extraNameChars(const Set &set) { return set.id == "ascii" ? QString() : QStringLiteral("[\\]|"); }

} // namespace Nat
