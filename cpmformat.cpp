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
#include "cpmformat.h"

#include <QFile>
#include <QRegularExpression>
#include <QMap>
#include <QSet>

namespace Cpm {

QString sideOrderName(SideOrder s) {
    switch (s) {
    case SideOrder::OutBack: return "outback";
    case SideOrder::OutOut: return "outout";
    default: return "alt";
    }
}

QString osName(Os o) {
    switch (o) {
    case Os::V3: return "3";
    case Os::P2dos: return "p2dos";
    case Os::Zsys: return "zsys";
    default: return "2.2";
    }
}

bool Format::derive(QString *err) {
    derived = false;
    if (seclen < 128 || seclen > 8192 || (seclen & (seclen - 1))) { *err = "sector length must be a power of two from 128 to 8192"; return false; }
    if (sectrk < 1 || sectrk > 255) { *err = "sectors per track must be 1-255"; return false; }
    if (tracks < 1 || tracks > 65535) { *err = "track count out of range"; return false; }
    if (blocksize < 1024 || blocksize > 16384 || (blocksize & (blocksize - 1))) { *err = "block size must be 1K, 2K, 4K, 8K or 16K"; return false; }
    if (maxdir < 1 || maxdir > 8192) { *err = "maxdir out of range"; return false; }
    physHeads = heads > 0 ? heads : (tracks > 86 && tracks % 2 == 0 ? 2 : 1);
    if (separateSides) physHeads = 2;
    if (physHeads > 2) { *err = "more than two sides"; return false; }
    if (physHeads == 1 && sides != SideOrder::Alt) sides = SideOrder::Alt;
    cylinders = separateSides ? tracks : (tracks + physHeads - 1) / physHeads;
    if (fixedHead < 0 || fixedHead > 1) fixedHead = 0;
    int reserved = bootsec >= 0 ? bootsec : boottrk * sectrk;
    if (reserved < 0 || reserved >= tracks * sectrk) { *err = "the boot area fills the disc"; return false; }
    if (acornDfs && (seclen != 256 || reserved < 2 || offsetSectors || separateSides)) {
        *err = "acorndfs needs 256-byte sectors and a boot area holding track 0's first two sectors";
        return false;
    }
    if (torch && (seclen != 256 || sectrk != 10 || tracks != 160 || heads != 2 || sides != SideOrder::Alt || reserved ||
                  offsetSectors || separateSides || skew > 1 || !skewtab.isEmpty() || t0sectrk || acornDfs)) {
        *err = "torchcpn needs 2 sides of 80 tracks of 10 x 256 bytes, cylinder by cylinder (sides alt), with no skew or boot area";
        return false;
    }
    if (t0sectrk > 0) {
        if (reserved < sectrk) { *err = "a track 0 of its own shape must be in the boot area (boottrk 1 or more)"; return false; }
        if (t0seclen < 128 || t0seclen > 8192 || (t0seclen & (t0seclen - 1)) || t0sectrk > 255) { *err = "track0: bad sector size or count"; return false; }
    }
    dataStart = offsetSectors + reserved;
    qint64 dataBytes = qint64(tracks * sectrk - reserved) * seclen;
    dsm = int(dataBytes / blocksize) - 1;
    if (dsm < 1 || dsm > 65535) { *err = "block count out of range"; return false; }
    ptr16 = dsm > 255 || ptr16Forced;
    ptrsPerEntry = ptr16 ? 8 : 16;
    int ext = logicalExtents > 0 ? logicalExtents : ptrsPerEntry * blocksize / 16384;
    if (ext < 1) { *err = "1K blocks with two-byte block pointers is not a valid CP/M layout"; return false; }
    exm = ext - 1;
    if (exm > 15) { *err = "extent mask too large"; return false; }
    drm = maxdir - 1;
    dirBlocks = dirblks > 0 ? dirblks : (maxdir * 32 + blocksize - 1) / blocksize;
    if (dirBlocks > 16 && dirblks == 0) { *err = "the directory needs more than 16 blocks"; return false; }
    if (dirBlocks >= dsm) { *err = "the directory fills the disc"; return false; }
    if (maxdir * 32 > dirBlocks * blocksize) { *err = "maxdir does not fit in dirblks"; return false; }
    secsPerBlock = qMax(1, blocksize / seclen);
    // skew
    xlat.clear();
    if (!skewtab.isEmpty()) {
        if (skewtab.size() != sectrk) { *err = "the skew table does not have one entry per sector"; return false; }
        QSet<int> seen;
        for (int v : skewtab) {
            if (v < 0 || v >= sectrk || seen.contains(v)) { *err = "the skew table is not a permutation of 0..sectrk-1"; return false; }
            seen.insert(v);
        }
        xlat = skewtab;
    } else {
        // cpmtools' algorithm: step by skew, moving on to the next free sector when one is taken
        QVector<bool> used(sectrk, false);
        int j = 0;
        for (int i = 0; i < sectrk; ++i) {
            while (used[j]) j = (j + 1) % sectrk;
            xlat.append(j);
            used[j] = true;
            j = (j + qMax(skew, 1)) % sectrk;
        }
    }
    derived = true;
    return true;
}

bool Format::locate(int lsec, int *cyl, int *head, int *idx) const {
    if (lsec < 0) return false;
    int t = lsec / sectrk, s = lsec % sectrk;
    int offTracks = offsetSectors / sectrk;
    if (t >= tracks + offTracks + (offsetSectors % sectrk ? 1 : 0)) return false;
    if (separateSides) {
        *cyl = t;
        *head = fixedHead;
    } else if (offsetSectors > 0 || sides == SideOrder::Alt || physHeads == 1) {
        *cyl = t / physHeads;
        *head = t % physHeads;
    } else if (sides == SideOrder::OutBack) {
        if (t < cylinders) { *cyl = t; *head = 0; } else { *cyl = 2 * cylinders - 1 - t; *head = 1; }
    } else {
        if (t < cylinders) { *cyl = t; *head = 0; } else { *cyl = t - cylinders; *head = 1; }
    }
    *idx = xlat.isEmpty() ? s : xlat[s];
    return *cyl >= 0;
}

qint64 Format::capacityBytes() const {
    if (torch) return qint64(tracks - 2) * sectrk * seclen;
    return qint64(dsm + 1) * blocksize;
}

bool Format::sameLayout(const Format &o) const {
    return torch == o.torch && seclen == o.seclen && sectrk == o.sectrk && tracks == o.tracks && physHeads == o.physHeads &&
           separateSides == o.separateSides &&
           (physHeads == 1 || sides == o.sides) && blocksize == o.blocksize && maxdir == o.maxdir &&
           dirBlocks == o.dirBlocks && dataStart == o.dataStart && xlat == o.xlat && dsm == o.dsm && exm == o.exm;
}

static QString kb(qint64 b) {
    if (b >= 1024 * 1024 && b % (1024 * 1024) == 0) return QString("%1M").arg(b / (1024 * 1024));
    if (b >= 10 * 1024 * 1024) return QString("%1M").arg(double(b) / (1024 * 1024), 0, 'f', 1);
    return QString("%1K").arg(b / 1024);
}

QString Format::summary() const {
    if (torch)
        return QString("Torch CP/N: %1 x %2 bytes/track, %3 tracks (2 sides, alt), 256 directory entries, %4")
            .arg(sectrk).arg(seclen).arg(tracks).arg(kb(capacityBytes()));
    QString skewText;
    if (!skewtab.isEmpty()) skewText = "skew table";
    else if (skew > 1) skewText = QString("skew %1").arg(skew);
    else skewText = "no skew";
    QString sideText = separateSides ? "per side; 2 sides, a disc on each" : physHeads == 1 ? "1 side" : QString("2 sides, %1").arg(sideOrderName(sides));
    QString boot = bootsec >= 0 ? QString("%1 boot sectors").arg(bootsec) : QString("%1 boot track%2").arg(boottrk).arg(boottrk == 1 ? "" : "s");
    QString t0 = t0sectrk > 0 ? QString(" (track 0: %1 x %2 %3)").arg(t0sectrk).arg(t0seclen).arg(t0fm ? "FM" : "MFM") : QString();
    return QString("%1 x %2 bytes/track, %3 tracks (%4), %5, %6, %7 blocks, %8 directory entries, CP/M %9, %10")
        .arg(sectrk).arg(seclen).arg(tracks).arg(sideText, skewText, boot + t0, kb(blocksize)).arg(maxdir)
        .arg(osName(os), kb(capacityBytes()));
}

QString Format::shortSummary() const {
    if (torch) return QString("%1x%2, %3 trk (2 sides alt), Torch CP/N filesystem").arg(sectrk).arg(seclen).arg(tracks);
    QString sk = !skewtab.isEmpty() ? QString("skew table") : skew > 1 ? QString("skew %1").arg(skew) : QString("no skew");
    QString sides = separateSides ? QString(" x 2 separate sides") : physHeads == 1 ? QString() : QString(" (2 sides %1)").arg(sideOrderName(this->sides));
    QString boot = bootsec >= 0 ? QString("%1 boot sec").arg(bootsec) : QString("boot %1").arg(boottrk);
    return QString("%1x%2, %3 trk%4, %5, %6, %7, %8 dir").arg(sectrk).arg(seclen).arg(tracks).arg(sides, sk, boot, kb(blocksize)).arg(maxdir);
}

QString Format::toDiskdef() const {
    QStringList l;
    l << "diskdef " + (name.isEmpty() ? QString("detected") : name);
    l << QString("  seclen %1").arg(seclen) << QString("  tracks %1").arg(tracks);
    if (separateSides) l << "  separatesides yes";
    else if (physHeads == 2) l << "  sides " + sideOrderName(sides);
    l << QString("  sectrk %1").arg(sectrk) << QString("  blocksize %1").arg(blocksize) << QString("  maxdir %1").arg(maxdir);
    if (dirblks > 0) l << QString("  dirblks %1").arg(dirblks);
    if (!skewtab.isEmpty()) {
        QStringList s;
        for (int v : skewtab) s << QString::number(v);
        l << "  skewtab " + s.join(',');
    } else {
        l << QString("  skew %1").arg(skew);
    }
    if (t0sectrk > 0)
        l << QString("  track0 %1 %2 %3%4").arg(t0sectrk).arg(t0seclen).arg(t0fm ? "fm" : "mfm")
                 .arg(t0firstId >= 0 ? QString(" %1").arg(t0firstId) : QString());
    if (acornDfs) l << "  acorndfs yes";
    if (torch) l << "  torchcpn yes";
    if (ptr16Forced) l << "  ptr16 yes";
    if (bootsec >= 0) l << QString("  bootsec %1").arg(bootsec);
    else l << QString("  boottrk %1").arg(boottrk);
    if (offsetSectors > 0) l << QString("  offset %1").arg(qint64(offsetSectors) * seclen);
    l << "  os " + osName(os);
    l << "end";
    return l.join('\n');
}

// ------------------------------------------------------------------------------------------- parsing

static bool parseOffset(const QString &v, int seclen, int sectrk, int *sectors) {
    QRegularExpression re("^(\\d+)\\s*([a-z]*)$", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(v.trimmed());
    if (!m.hasMatch()) return false;
    qint64 n = m.captured(1).toLongLong();
    QString u = m.captured(2).toLower();
    qint64 bytes;
    if (u == "t" || u == "trk" || u == "tracks") { *sectors = int(n * sectrk); return true; }
    if (u.isEmpty()) bytes = n;
    else if (u == "k" || u == "kb") bytes = n * 1024;
    else if (u == "m" || u == "mb") bytes = n * 1024 * 1024;
    else return false;
    if (bytes % seclen) return false;
    *sectors = int(bytes / seclen);
    return true;
}

QVector<Format> parseDiskdefs(const QString &text, const QString &origin, QStringList *warnings) {
    QVector<Format> out;
    Format f;
    bool in = false, bad = false;
    QString offsetText;
    int lineNo = 0;
    QString badWhy;
    for (QString line : text.split('\n')) {
        ++lineNo;
        // a comment runs from '#' to the end of the line, except inside a quoted desc/source string
        int q = line.indexOf('"'), h = line.indexOf('#');
        if (h >= 0 && (q < 0 || h < q)) line = line.left(h);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        QString key = line.section(QRegularExpression("\\s+"), 0, 0).toLower();
        QString val = line.mid(key.size()).trimmed();
        if (key == "diskdef") {
            f = Format();
            f.name = val.section(QRegularExpression("\\s+"), 0, 0);
            f.origin = origin;
            in = true; bad = false; badWhy.clear(); offsetText.clear();
            continue;
        }
        if (!in) continue;
        if (key == "end") {
            in = false;
            if (!bad && !offsetText.isEmpty() && !parseOffset(offsetText, f.seclen, f.sectrk, &f.offsetSectors)) {
                bad = true; badWhy = "offset '" + offsetText + "' not understood";
            }
            QString e;
            if (!bad && !f.derive(&e)) { bad = true; badWhy = e; }
            if (bad) { if (warnings) *warnings << QString("%1: diskdef %2 skipped: %3").arg(origin, f.name, badWhy); }
            else out.append(f);
            continue;
        }
        bool ok = true;
        auto num = [&](int *dst) { *dst = val.section(QRegularExpression("\\s+"), 0, 0).toInt(&ok, 0); };
        auto unq = [](QString s) { s = s.trimmed(); if (s.startsWith('"') && s.endsWith('"') && s.size() >= 2) s = s.mid(1, s.size() - 2); return s; };
        if (key == "seclen") num(&f.seclen);
        else if (key == "tracks") num(&f.tracks);
        else if (key == "sectrk") num(&f.sectrk);
        else if (key == "blocksize") num(&f.blocksize);
        else if (key == "maxdir") num(&f.maxdir);
        else if (key == "dirblks") num(&f.dirblks);
        else if (key == "boottrk") num(&f.boottrk);
        else if (key == "bootsec") num(&f.bootsec);
        else if (key == "skew") num(&f.skew);
        else if (key == "heads") num(&f.heads);
        else if (key == "secbase") num(&f.secbase);
        else if (key == "datasect") num(&f.dataId);
        else if (key == "imdmode") num(&f.imdMode);
        else if (key == "logicalextents") num(&f.logicalExtents);
        else if (key == "offset") offsetText = val;
        else if (key == "track0") {
            QStringList p = val.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            bool a = false, b = false;
            if (p.size() >= 3) { f.t0sectrk = p[0].toInt(&a); f.t0seclen = p[1].toInt(&b); }
            ok = a && b && (p.size() >= 3) && (p[2].toLower() == "fm" || p[2].toLower() == "mfm");
            if (ok) f.t0fm = p[2].toLower() == "fm";
            if (ok && p.size() >= 4) f.t0firstId = p[3].toInt(&ok, 0);
        }
        else if (key == "skewtab") {
            f.skewtab.clear();
            for (const QString &p : val.split(',', Qt::SkipEmptyParts)) {
                int v = p.trimmed().toInt(&ok);
                if (!ok) break;
                f.skewtab.append(v);
            }
        } else if (key == "sides") {
            QString v = val.toLower();
            if (v == "alt") f.sides = SideOrder::Alt;
            else if (v == "outback") f.sides = SideOrder::OutBack;
            else if (v == "outout") f.sides = SideOrder::OutOut;
            else ok = false;
            if (ok && f.heads == 0) f.heads = 2;
        } else if (key == "os") {
            QString v = val.toLower();
            if (v == "2.2" || v == "2") f.os = Os::V22;
            else if (v == "3") f.os = Os::V3;
            else if (v == "p2dos") f.os = Os::P2dos;
            else if (v == "zsys") f.os = Os::Zsys;
            else ok = false;
        } else if (key == "separatesides") {
            f.separateSides = val.toLower().startsWith('y');
        } else if (key == "acorndfs") {
            f.acornDfs = val.toLower().startsWith('y');
        } else if (key == "ptr16") {
            f.ptr16Forced = val.toLower().startsWith('y');
        } else if (key == "torchcpn") {
            f.torch = val.toLower().startsWith('y');
        } else if (key == "fm") {
            f.fm = val.toLower().startsWith('y');
        } else if (key == "datarate") {
            f.datarate = val.toUpper();
        } else if (key == "desc") {
            f.desc = unq(val);
        } else if (key == "source") {
            f.source = unq(val);
        } else if (key.startsWith("libdsk:")) {
            // libdsk's own format names: not needed here
        } else {
            ok = true;           // unknown keys are ignored, as cpmtools does with keys it doesn't know
        }
        if (!ok && !bad) { bad = true; badWhy = QString("line %1: '%2' not understood").arg(lineNo).arg(line); }
    }
    return out;
}

// ------------------------------------------------------------------------------------------- the library

static QVector<Format> &lib() { static QVector<Format> l; return l; }
static QStringList &extraFiles() { static QStringList f; return f; }

static QString readResource(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

// "describe NAME text" lines: descriptions for definitions that come from elsewhere (cpmtools' file).
static QMap<QString, QString> describes(const QString &text) {
    QMap<QString, QString> m;
    QRegularExpression re("^\\s*describe\\s+(\\S+)\\s+\"([^\"]*)\"", QRegularExpression::CaseInsensitiveOption);
    for (const QString &line : text.split('\n')) {
        auto mt = re.match(line);
        if (mt.hasMatch()) m[mt.captured(1).toLower()] = mt.captured(2);
    }
    return m;
}

static void build(QStringList *warnings) {
    QVector<Format> all;
    all += parseDiskdefs(readResource(":/diskdefs/builtin"), "built-in", warnings);
    all += parseDiskdefs(readResource(":/diskdefs/cpmtools"), "cpmtools", warnings);
    for (const QString &p : extraFiles()) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) { if (warnings) *warnings << p + ": " + f.errorString(); continue; }
        all += parseDiskdefs(QString::fromUtf8(f.readAll()), p, warnings);
    }
    QSet<QString> names;
    lib().clear();
    for (const Format &f : all) {
        if (names.contains(f.name.toLower())) continue;
        names.insert(f.name.toLower());
        lib().append(f);
    }
    QMap<QString, QString> d = describes(readResource(":/diskdefs/builtin"));
    for (Format &f : lib())
        if (f.desc.isEmpty() && d.contains(f.name.toLower())) f.desc = d[f.name.toLower()];
}

const QVector<Format> &library() {
    if (lib().isEmpty()) build(nullptr);
    return lib();
}

void setExtraDiskdefFiles(const QStringList &paths, QStringList *warnings) {
    extraFiles() = paths;
    build(warnings);
}

QStringList extraDiskdefFiles() { return extraFiles(); }

const Format *findFormat(const QString &name) {
    for (const Format &f : library())
        if (f.name.compare(name, Qt::CaseInsensitive) == 0) return &f;
    return nullptr;
}

QByteArray acornDfsCatalogue(int sectorsPerSide) {
    QByteArray c(512, '\0');
    c.replace(0, 8, "Acorn CP");                 // title, first 8 characters (sector 0)
    c.replace(8, 7, "CPMDISC");
    c[15] = char('$' | 0x80);                    // directory $, locked
    c.replace(256, 4, "/M  ");                   // title, last 4 characters (sector 1)
    c[256 + 5] = 8;                              // one file
    c[256 + 6] = char((sectorsPerSide >> 8) & 3);   // boot option 0, sector count high bits
    c[256 + 7] = char(sectorsPerSide & 0xFF);
    const int start = 2, len = (sectorsPerSide - start) * 256;
    c[256 + 12] = char(len & 0xFF);              // load and exec 0
    c[256 + 13] = char((len >> 8) & 0xFF);
    c[256 + 14] = char(((len >> 16) & 3) << 4 | ((start >> 8) & 3));
    c[256 + 15] = char(start & 0xFF);
    return c;
}

bool isAcornDfsCatalogue(const QByteArray &b) {
    if (b.size() < 512 || quint8(b[256 + 5]) != 8 || b.mid(8, 7) != "CPMDISC" || (quint8(b[15]) & 0x7F) != '$') return false;
    int sectors = (quint8(b[256 + 6]) & 3) << 8 | quint8(b[256 + 7]);
    return sectors > 2;
}

} // namespace Cpm
