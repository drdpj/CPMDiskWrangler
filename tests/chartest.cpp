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
// chartest - national character sets (natchars.h): the mappings, names written and read with them, and, given real
// discs, whether the evidence scan and the guess read them right.
//   chartest [<image> <set id | none>]...
// e.g.  chartest comal1.imd dk-no  plain.imd none     (an image given with a set must be recognised as using it)
#include <QApplication>
#include <QSet>
#include <cstdio>
#include "../src/cpmformat.h"
#include "../src/cpmfs.h"
#include "../src/detect.h"
#include <diskwrangler/device/container.h>
#include "../src/natchars.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } } while (0)

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QStringList warn;
    Cpm::setExtraDiskdefFiles({}, &warn);
    const QString pos = Nat::positions();

    printf("== the sets\n");
    CHECK(Nat::sets().first().id == "ascii" && Nat::find("nonsense").id == "ascii", "ascii is first and the fallback");
    for (const Nat::Set &s : Nat::sets()) {
        CHECK(s.shown.size() == pos.size(), s.id + ": one character for each position");
        QSet<QChar> seen;
        for (QChar c : s.shown) { CHECK(!seen.contains(c), s.id + ": a letter used twice: " + QString(c)); seen.insert(c); }
        // every position survives a trip to the host and back, exactly
        const QString all = "A[B\\C]D{E|F}G@H^I`J~K";
        CHECK(Nat::fromHost(Nat::toHost(all, s), s, false) == all, s.id + ": round trip " + Nat::toHost(all, s));
        // upper case names: the lower case letters come back as the upper case positions' ... or themselves for ascii
        const QString up = "BR[D\\E]F";
        CHECK(Nat::fromHost(Nat::toHost(up, s), s, true) == up, s.id + ": upper-case round trip");
        CHECK(Nat::toHost("PLAIN 123.TXT", s) == "PLAIN 123.TXT", s.id + ": plain names are untouched");
        CHECK(Nat::toHost("#$", s) == "#$", s.id + ": # and $ are never mapped");
        printf("  %-6s %s\n", qPrintable(s.id), qPrintable(Nat::toHost("A[B\\C]D{E|F}G@H^I`J~K", s)));
    }
    const Nat::Set dk = Nat::find("dk-no"), de = Nat::find("de"), se = Nat::find("se-fi");
    CHECK(Nat::toHost("br|kregning", dk) == QString::fromUtf8("br\xC3\xB8kregning"), "Danish: br|kregning is brøkregning");
    CHECK(Nat::toHost("}rsl|n", dk) == QString::fromUtf8("\xC3\xA5rsl\xC3\xB8n"), "Danish: }rsl|n is årsløn");
    CHECK(Nat::fromHost(QString::fromUtf8("br\xC3\xB8kregning"), dk) == "BR\\KREGNING", "importing brøkregning gives BR\\KREGNING");
    CHECK(Nat::fromHost(QString::fromUtf8("br\xC3\xB8kregning"), dk, false) == "br|kregning", "not upper-cased: br|kregning");
    CHECK(Nat::fromHost(QString::fromUtf8("Stra\xC3\x9F" "e"), de) == "STRA~E", "German: ß is kept, not SS");
    CHECK(Nat::fromHost(QString::fromUtf8("\xC3\xA4"), se) == "[", "Swedish: ä (upper-cased Ä) is [");
    CHECK(Nat::fromHost(QString::fromUtf8("\xC3\xA7"), dk) == QString::fromUtf8("\xC3\x87"), "a letter the set has no place for stays a letter");
    CHECK(Nat::extraNameChars(Nat::find("ascii")).isEmpty() && !Nat::extraNameChars(dk).isEmpty(), "extra name characters only for national sets");

    printf("== which set a host name needs\n");
    CHECK(Nat::foreignLetters({QString::fromUtf8("l\xC3\xB8nseddel.l"), "plain.txt"}) == QString::fromUtf8("\xC3\x98"), "lønseddel.l has the letter Ø");
    CHECK(Nat::suggestFor({QString::fromUtf8("l\xC3\xB8nseddel.l")}) == "dk-no", "ø means Danish/Norwegian");
    CHECK(Nat::suggestFor({QString::fromUtf8("\xC3\xA6\xC3\xB8\xC3\xA5")}) == "dk-no", "æøå means Danish/Norwegian");
    CHECK(Nat::suggestFor({QString::fromUtf8("\xC3\xA4\xC3\xB6")}) == "se-fi", "äö: Swedish/Finnish comes first");
    CHECK(Nat::suggestFor({QString::fromUtf8("stra\xC3\x9F" "e")}) == "de", "ß means German");
    CHECK(Nat::candidatesFor({QString::fromUtf8("\xC3\xA4\xC3\xB6")}) == QStringList({"se-fi", "de"}), "äö: Swedish/Finnish and German both have them");
    CHECK(Nat::suggestFor({"plain.txt"}).isEmpty() && Nat::suggestFor({QString::fromUtf8("\xD0\xB6")}).isEmpty(), "none for plain names or letters no set has");

    printf("== names on a disc\n");
    QString err;
    dw::device::Container img;
    CHECK(Cpm::makeBlank(&img, dw::device::Kind::Imd, *Cpm::findFormat("ibm-3740"), false, QString(), &err), err);
    Cpm::Volume v;
    CHECK(v.attach(&img, *Cpm::findFormat("ibm-3740"), &err), err);
    CHECK(!v.addFile(0, "BR\\D.TXT", "x", 0, QDateTime(), false, &err), "a name with \\ is refused when no set is chosen");
    printf("  refused as it should be: %s\n", qPrintable(err));
    v.setExtraNameChars(Nat::extraNameChars(dk));
    CHECK(v.addFile(0, "BR\\D.TXT", "bread", 0, QDateTime(), false, &err), "...and accepted with a national set: " + err);
    CHECK(v.addFile(0, "S|G.TXT", "search", 0, QDateTime(), false, &err), "| accepted too: " + err);
    CHECK(v.indexOf(0, "BR\\D.TXT") >= 0, "found again by its disc name");
    CHECK(v.rename(0, "BR\\D.TXT", 0, "BR[D.TXT", &err), "rename to a name with [: " + err);
    CHECK(Nat::toHost(v.files()[v.indexOf(0, "BR[D.TXT")].name, dk) == QString::fromUtf8("BR\xC3\x86" "D.TXT"), "shown as BRÆD.TXT");
    CHECK(Cpm::Volume::toCpmName(Nat::fromHost(QString::fromUtf8("br\xC3\xB8" "d.txt"), dk), Nat::extraNameChars(dk)) == "BR\\D.TXT",
          "a host name brød.txt becomes BR\\D.TXT");
    CHECK(Cpm::Volume::toCpmName("br\\d.txt") == "BR-D.TXT", "...and without a set \\ is still replaced by -");

    for (int i = 1; i + 1 < argc; i += 2) {
        printf("== %s (expect %s)\n", argv[i], argv[i + 1]);
        dw::device::Container im;
        if (!im.open(argv[i], &err)) { CHECK(false, QString("open: ") + err); continue; }
        Detect::Result r = Detect::detect(im);
        Cpm::Volume a, b;
        QVector<const Cpm::Store *> vols;
        if (!r.ranked.isEmpty()) {
            Cpm::Format f = r.ranked.first().fmt;
            f.fixedHead = 0; f.derived = false;
            if (a.attach(&im, f, &err)) vols << &a;
            if (f.separateSides) { f.fixedHead = 1; f.derived = false; if (b.attach(&im, f, &err)) vols << &b; }
        }
        CHECK(!vols.isEmpty(), "the disc is read");
        Nat::Evidence ev = Nat::scan(vols);
        printf("  %d file names, %d text places, guess '%s'; e.g. %s\n", ev.nameFiles, ev.textHits, qPrintable(ev.guess),
               qPrintable(ev.names.value(0)));
        const QString want = argv[i + 1];
        if (want == "none") CHECK(!ev.any(), "this disc should not look like it uses a national set");
        else {
            CHECK(ev.any(), "this disc should look like it uses a national set");
            CHECK(ev.guess == want, QString("the guess is '%1', expected '%2'").arg(ev.guess, want));
        }
    }
    printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
