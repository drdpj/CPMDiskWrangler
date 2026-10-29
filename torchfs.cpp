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
// torchfs.cpp - Torch CP/N's filesystem (see torchfs.h), and the Cpm::Volume methods that use it when the format
// says "torchcpn yes". The rest of the program sees the same Volume either way.
#include "torchfs.h"

#include "cpmfs.h"

#include <QSet>

namespace Torch {

QByteArray formatterSectors() {
    QByteArray b(512, '\0');
    for (int i = 0; i < 256; ++i) b[i] = char((0xD6 + i) & 0xFF);
    b[256 + 254] = char(0x85);
    return b;
}

QByteArray emptyMap() {
    QByteArray m(512, '\0');
    for (int t = 0; t < 256; ++t) setWord(&m, t, t < 2 ? 0xFFFF : MapFixed);     // tracks 0-1: directory, map, formatter's
    return m;
}

bool entryIsDeleted(const QByteArray &e) { return e.size() >= EntrySize && word(e, 0) == NoData; }
bool entryIsFree(const QByteArray &e) { return e.count('\0') == EntrySize || entryIsDeleted(e); }

bool entryIsFile(const QByteArray &e) {
    if (e.size() < EntrySize || quint8(e[4]) > 31) return false;
    bool any = false;
    for (int k = 5; k < 16; ++k) {
        int c = quint8(e[k]) & 0x7F;
        if (c == 0) continue;                            // seen in an extension's last byte: read as a space
        if (c < 0x20 || c == 0x7F) return false;
        if (k < 13 && c != ' ') any = true;
    }
    return any;
}

qint64 entryRecords(const QByteArray &e) {
    if (entryIsDeleted(e)) return 0;
    return qint64(word(e, 1)) + 1;
}

Walk walk(const SectorReader &read, quint16 first, qint64 records) {
    Walk w;
    if (first == NoData) return w;                       // a deleted entry: no sectors
    auto ok = [&](quint16 a, const char *what) {
        if (!validAddr(a)) { w.errors << QString("%1 %2 is not on the disc").arg(what).arg(a & AddrMask, 4, 16, QChar('0')); return false; }
        if (lsecOf(a) < Reserved) { w.errors << QString("%1 points into the directory tracks").arg(what); return false; }
        return true;
    };
    if (!ok(first, "its first index sector")) return w;
    QVector<int> l3;
    QByteArray s;
    w.index << lsecOf(first);
    if (first & L2Flag) {
        if (!read(lsecOf(first), &s)) { w.errors << "its L2 index sector can't be read"; return w; }
        bool ended = false;
        for (int i = 0; i < PtrsPerSector; ++i) {
            quint16 p = word(s, i);
            if (!p) { ended = true; continue; }
            if (ended) { w.errors << "its L2 index has a gap"; break; }
            if (p & ~AddrMask) w.warnings << "an L2 index entry has flag bits set";
            if (!ok(p, "an L3 index sector")) return w;
            l3 << lsecOf(p);
            w.index << lsecOf(p);
        }
    } else {
        l3 << lsecOf(first);
    }
    for (int n : l3) {
        if (!read(n, &s)) { w.errors << "an L3 index sector can't be read"; return w; }
        bool ended = false;
        for (int i = 0; i < PtrsPerSector; ++i) {
            quint16 p = word(s, i);
            if (!p) { ended = true; continue; }
            if (ended) { w.errors << "an L3 index has a gap"; break; }
            if (!ok(p, "a data sector")) return w;
            w.data << lsecOf(p);
            w.flags << quint16(p & ~AddrMask);
        }
    }
    qint64 want = (records + 1) / 2;
    if (w.data.size() != want)
        w.errors << QString("%1 data sectors for %2 records (%3 expected)").arg(w.data.size()).arg(records).arg(want);
    for (int i = 0; i < w.flags.size(); ++i) {
        bool last = i == w.flags.size() - 1;
        quint16 expect = last && (records & 1) ? FirstRecord : quint16(FirstRecord | SecondRecord);
        if (w.flags[i] != expect) { w.warnings << "a data sector's record flags don't match the record count"; break; }
    }
    return w;
}

Scan scan(const SectorReader &read) {
    Scan r;
    QByteArray dir, s, map;
    for (int n = 0; n < DirSectors; ++n) {
        if (!read(n, &s) || s.size() != SecLen) return r;
        dir += s;
    }
    QByteArray m0, m1;
    if (!read(MapSector, &m0) || !read(MapSector + 1, &m1)) return r;
    map = m0 + m1;
    r.readable = true;
    r.mapOk = true;
    for (int t = 0; t < 256; ++t) {
        quint16 v = word(map, t);
        if ((v & MapFixed) != MapFixed || (t >= Tracks && !beyondOk(v)) || (t < 2 && v != 0xFFFF)) r.mapOk = false;
    }
    QByteArray f0, f1;
    r.signature = read(FormatterSector, &f0) && read(FormatterSector + 1, &f1);
    if (r.signature) {
        QByteArray f = f0 + f1, want = formatterSectors();
        f[256 + 254] = want[256 + 254];                  // the one byte that varies (85, 80, 47 seen)
        r.signature = f == want;
    }
    QSet<int> seen;
    for (int i = 0; i < Entries; ++i) {
        QByteArray e = dir.mid(i * EntrySize, EntrySize);
        if (e.count('\0') == EntrySize || entryIsDeleted(e)) continue;
        if (!entryIsFile(e)) { ++r.bad; continue; }
        ++r.files;
        QByteArray n11;
        for (int k = 5; k < 16; ++k) n11 += char((quint8(e[k]) & 0x7F) ? (quint8(e[k]) & 0x7F) : ' ');
        r.names << QString("%1:%2").arg(quint8(e[4])).arg(QString::fromLatin1(n11));
        Walk w = walk(read, word(e, 0), entryRecords(e));
        if (!w.errors.isEmpty()) ++r.badPtr;
        else ++r.exact;
        for (int n : w.index + w.data) {
            if (seen.contains(n)) ++r.cross;
            seen.insert(n);
        }
    }
    return r;
}

} // namespace Torch

// ================================================================================================ Cpm::Volume

namespace Cpm {

using namespace Torch;

#define TXN_BEGIN auto snap__ = m_img->snapshot(); auto fail = [&](const QString &e) { *err = e; m_img->restore(snap__); reload(); return false; };

static QByteArray name11Of(const QByteArray &e) {
    QByteArray n;
    for (int k = 5; k < 16; ++k) { int c = quint8(e[k]) & 0x7F; n += char(c ? c : ' '); }
    return n;
}

static quint16 attrsOf(const QByteArray &e) {
    quint16 a = 0;
    static const quint16 f[4] = {AttrF1, AttrF2, AttrF3, AttrF4};
    for (int k = 0; k < 4; ++k) if (quint8(e[5 + k]) & 0x80) a |= f[k];
    if (quint8(e[13]) & 0x80) a |= AttrRO;
    if (quint8(e[14]) & 0x80) a |= AttrSYS;
    if (quint8(e[15]) & 0x80) a |= AttrARC;
    return a;
}

// The 11 name bytes with the attributes in bit 7, into entry bytes 5-15.
static void putName(QByteArray *e, const QByteArray &n11, quint16 a) {
    static const quint16 bit[11] = {AttrF1, AttrF2, AttrF3, AttrF4, 0, 0, 0, 0, AttrRO, AttrSYS, AttrARC};
    for (int k = 0; k < 11; ++k) (*e)[5 + k] = char((quint8(n11[k]) & 0x7F) | ((a & bit[k]) ? 0x80 : 0));
}

bool Volume::torchSector(int lsec, QByteArray *out, QString *err) const {
    return readBytes(qint64(lsec) * SecLen, SecLen, out, err);
}

bool Volume::torchWriteSector(int lsec, const QByteArray &d, QString *err) {
    QByteArray s = d.left(SecLen);
    if (s.size() < SecLen) s.append(QByteArray(SecLen - s.size(), char(0xE5)));
    return writeBytes(qint64(lsec) * SecLen, s, err);
}

SectorReader Volume::torchReader() const {
    return [this](int n, QByteArray *out) { QString e; return torchSector(n, out, &e); };
}

bool Volume::torchParse(QString *err) {
    QByteArray dir;
    if (!readBytes(0, DirSectors * SecLen, &dir, err)) return false;
    SectorReader read = torchReader();
    for (int i = 0; i < Entries; ++i) {
        QByteArray e = dir.mid(i * EntrySize, EntrySize);
        m_dir << e;
        if (entryIsFree(e) || !entryIsFile(e)) continue;
        File f;
        f.user = quint8(e[4]);
        f.name11 = name11Of(e);
        f.name = nameFrom11(f.name11);
        f.attrs = attrsOf(e);
        f.entries << i;
        f.records = entryRecords(e);
        f.size = f.records * 128;
        Walk w = walk(read, word(e, 0), f.records);
        f.blocks = w.index.size() + w.data.size();
        if (!w.errors.isEmpty()) f.problem = w.errors.join("; ");
        m_files << f;
    }
    return true;
}

bool Volume::torchReadFile(const File &f, QByteArray *out, QString *err) const {
    const QByteArray &e = m_dir[f.entries.value(0)];
    Walk w = walk(torchReader(), word(e, 0), f.records);
    if (!w.errors.isEmpty()) { *err = f.name + ": " + w.errors.first(); return false; }
    QByteArray d, s;
    for (int n : w.data) {
        if (!torchSector(n, &s, err)) { *err = f.name + ": " + *err; return false; }
        d += s;
    }
    *out = d.left(int(f.size));
    return true;
}

// Sectors in use: the first two tracks, and every sector a directory entry reaches. *errors: what a walk found wrong,
// and sectors reached twice.
QVector<bool> Volume::torchInUse(QStringList *errors, QStringList *warnings) const {
    QVector<bool> used(Sectors, false);
    for (int n = 0; n < Reserved; ++n) used[n] = true;
    SectorReader read = torchReader();
    for (int i = 0; i < m_dir.size(); ++i) {
        const QByteArray &e = m_dir[i];
        if (entryIsFree(e) || !entryIsFile(e)) continue;
        QString name = QString("%1:%2").arg(quint8(e[4])).arg(nameFrom11(name11Of(e)));
        Walk w = walk(read, word(e, 0), entryRecords(e));
        if (errors) for (const QString &x : w.errors) *errors << name + ": " + x;
        if (warnings) for (const QString &x : w.warnings) *warnings << name + ": " + x;
        for (int n : w.index + w.data) {
            if (used[n] && errors) *errors << QString("%1: sector %2 is used twice").arg(name).arg(n);
            used[n] = true;
        }
    }
    return used;
}

int Volume::torchFreeSectors() const {
    int n = 0;
    for (bool u : torchInUse()) if (!u) ++n;
    return n;
}

Report Volume::torchCheck() const {
    Report r;
    for (int i = 0; i < m_dir.size(); ++i)
        if (!entryIsFree(m_dir[i]) && !entryIsFile(m_dir[i]))
            r.errors << QString("directory entry %1 is not a file entry (%2)").arg(i).arg(QString(m_dir[i].toHex(' ')));
    QVector<bool> used = torchInUse(&r.errors, &r.warnings);
    QByteArray map;
    QString e;
    if (!readBytes(qint64(MapSector) * SecLen, 2 * SecLen, &map, &e)) { r.errors << "the allocation map can't be read: " + e; return r; }
    int lost = 0, unmarked = 0;
    for (int t = 0; t < 256; ++t) {
        quint16 v = word(map, t);
        if ((v & MapFixed) != MapFixed || (t >= Tracks && !beyondOk(v))) {
            r.warnings << QString("allocation map word %1 is %2 (expected FC00 in its top bits; FC00 or FFFF beyond the disc)").arg(t).arg(v, 4, 16, QChar('0'));
            continue;
        }
        if (t >= Tracks) continue;
        for (int s = 0; s < SecPerTrack; ++s) {
            bool marked = v & (1 << s), u = used[t * SecPerTrack + s];
            if (u && !marked) ++unmarked;
            if (marked && !u) ++lost;
        }
    }
    if (unmarked) r.errors << QString("%1 sector%2 in use but free in the allocation map").arg(unmarked).arg(unmarked == 1 ? " is" : "s are");
    if (lost) r.warnings << QString("%1 sector%2 marked in use in the allocation map but no file uses %3 (lost space)")
                                .arg(lost).arg(lost == 1 ? " is" : "s are").arg(lost == 1 ? "it" : "them");
    return r;
}

// Sets (or clears) the map bits of these sectors.
static void markMap(QByteArray *map, const QVector<int> &sectors, bool inUse) {
    for (int n : sectors) {
        int t = n / SecPerTrack, s = n % SecPerTrack;
        quint16 v = word(*map, t);
        setWord(map, t, inUse ? quint16(v | (1 << s)) : quint16(v & ~(1 << s)));
    }
}

bool Volume::torchAdd(int user, const QString &nameIn, const QByteArray &data, quint16 attrs, bool replace, QString *err) {
    QString name = nameIn.toUpper(), why;
    if (!validName(name, &why)) { *err = name + ": " + why; return false; }
    if (user < 0 || user > MaxUser) { *err = QString("user number %1 is out of range (0-31)").arg(user); return false; }
    TXN_BEGIN
    QString er;
    int ix = indexOf(user, name);
    if (ix >= 0) {
        if (!replace) return fail(QString("%1 already exists in user %2").arg(name).arg(user));
        if (m_files[ix].attrs & AttrRO) return fail(QString("%1 is read-only").arg(name));
        if (!torchDelete(user, name, &er)) return fail(er);
    }
    // an empty file gets one record of ^Z (address FFFF marks a deleted entry, so an entry can't be without data)
    qint64 records = qMax<qint64>(1, (data.size() + 127) / 128);
    if (records > 65536) return fail(name + ": too large for a Torch CP/N disc");
    QByteArray padded = data;
    padded.append(QByteArray(int(records * 128 - data.size()), char(0x1A)));
    int nData = int((records + 1) / 2);
    int nIndex = nData <= PtrsPerSector ? 1 : 1 + (nData + PtrsPerSector - 1) / PtrsPerSector;
    if (nIndex > 1 + PtrsPerSector) return fail(name + ": too large for a Torch CP/N disc");
    int slot = -1;
    for (int i = 0; i < m_dir.size() && slot < 0; ++i) if (entryIsFree(m_dir[i])) slot = i;   // unused or deleted
    if (slot < 0) return fail("the directory is full");
    // free: reached by no file and not marked in the map (a sector the map holds is never taken, even if lost)
    QByteArray map;
    if (!readBytes(qint64(MapSector) * SecLen, 2 * SecLen, &map, &er)) return fail(er);
    QVector<bool> used = torchInUse();
    QVector<int> got;
    for (int n = Reserved; n < Sectors && got.size() < nIndex + nData; ++n)
        if (!used[n] && !(word(map, n / SecPerTrack) & (1 << (n % SecPerTrack)))) got << n;
    if (got.size() < nIndex + nData) return fail(QString("the disc is full (%1 needs %2 sectors)").arg(name).arg(nIndex + nData));
    QVector<int> index = got.mid(0, nIndex), dataSecs = got.mid(nIndex);
    for (int k = 0; k < nData; ++k)
        if (!torchWriteSector(dataSecs[k], padded.mid(k * SecLen, SecLen), &er)) return fail(er);
    // L3 sectors (the last index sectors), then the L2 that lists them
    QVector<int> l3 = nIndex == 1 ? index : index.mid(1);
    for (int j = 0; j < l3.size(); ++j) {
        QByteArray s(SecLen, '\0');
        for (int i = 0; i < PtrsPerSector; ++i) {
            int k = j * PtrsPerSector + i;
            if (k >= nData) break;
            bool second = qint64(k) * 2 + 1 < records;
            setWord(&s, i, quint16(addrOf(dataSecs[k]) | FirstRecord | (second ? SecondRecord : 0)));
        }
        if (!torchWriteSector(l3[j], s, &er)) return fail(er);
    }
    if (nIndex > 1) {
        QByteArray s(SecLen, '\0');
        for (int j = 0; j < l3.size(); ++j) setWord(&s, j, addrOf(l3[j]));
        if (!torchWriteSector(index[0], s, &er)) return fail(er);
    }
    markMap(&map, got, true);
    if (!writeBytes(qint64(MapSector) * SecLen, map, &er)) return fail(er);
    QByteArray e(EntrySize, '\0');
    setWord(&e, 0, quint16(addrOf(index[0]) | (nIndex > 1 ? L2Flag : 0)));
    setWord(&e, 1, quint16(records - 1));
    e[4] = char(user);
    putName(&e, name11(name), attrs);
    if (!writeBytes(qint64(slot) * EntrySize, e, &er)) return fail(er);
    if (!parse(&er)) return fail(er);
    return true;
}

bool Volume::torchDelete(int user, const QString &name, QString *err) {
    int ix = indexOf(user, name.toUpper());
    if (ix < 0) { *err = QString("%1:%2 not found").arg(user).arg(name); return false; }
    TXN_BEGIN
    QString er;
    int slot = m_files[ix].entries.value(0);
    const QByteArray e = m_dir[slot];
    Walk w = walk(torchReader(), word(e, 0), entryRecords(e));
    QByteArray map;
    if (!readBytes(qint64(MapSector) * SecLen, 2 * SecLen, &map, &er)) return fail(er);
    // a damaged chain frees only what it reached, and only sectors no other file reaches. The entry is marked deleted
    // as CP/N does it: index address FFFF, the name, user and record count kept
    QVector<int> mine = w.index + w.data;
    QByteArray dead = e;
    setWord(&dead, 0, NoData);
    m_dir[slot] = dead;
    QVector<bool> others = torchInUse();
    QVector<int> freed;
    for (int n : mine) if (n >= Reserved && !others[n]) freed << n;
    markMap(&map, freed, false);
    if (!writeBytes(qint64(MapSector) * SecLen, map, &er)) return fail(er);
    if (!writeBytes(qint64(slot) * EntrySize, dead, &er)) return fail(er);
    if (!parse(&er)) return fail(er);
    return true;
}

bool Volume::torchRename(int user, const QString &name, int newUser, const QString &newNameIn, QString *err) {
    QString newName = newNameIn.toUpper(), why;
    if (!validName(newName, &why)) { *err = newName + ": " + why; return false; }
    if (newUser < 0 || newUser > MaxUser) { *err = QString("user number %1 is out of range (0-31)").arg(newUser); return false; }
    int ix = indexOf(user, name.toUpper());
    if (ix < 0) { *err = QString("%1:%2 not found").arg(user).arg(name); return false; }
    int other = indexOf(newUser, newName);
    if (other >= 0 && other != ix) { *err = QString("%1 already exists in user %2").arg(newName).arg(newUser); return false; }
    TXN_BEGIN
    QString er;
    int slot = m_files[ix].entries.value(0);
    QByteArray e = m_dir[slot];
    e[4] = char(newUser);
    putName(&e, name11(newName), m_files[ix].attrs);
    if (!writeBytes(qint64(slot) * EntrySize, e, &er)) return fail(er);
    if (!parse(&er)) return fail(er);
    return true;
}

bool Volume::torchSetAttrs(int user, const QString &name, quint16 attrs, QString *err) {
    int ix = indexOf(user, name.toUpper());
    if (ix < 0) { *err = QString("%1:%2 not found").arg(user).arg(name); return false; }
    TXN_BEGIN
    QString er;
    int slot = m_files[ix].entries.value(0);
    QByteArray e = m_dir[slot];
    putName(&e, m_files[ix].name11, attrs);
    if (!writeBytes(qint64(slot) * EntrySize, e, &er)) return fail(er);
    if (!parse(&er)) return fail(er);
    return true;
}

// An empty disc: a zeroed directory, the map of a disc with no files, and the formatter's two sectors.
bool Volume::torchInit(QString *err) {
    TXN_BEGIN
    QString er;
    if (!writeBytes(0, QByteArray(DirSectors * SecLen, '\0'), &er) ||
        !writeBytes(qint64(MapSector) * SecLen, emptyMap(), &er) ||
        !writeBytes(qint64(FormatterSector) * SecLen, formatterSectors(), &er))
        return fail(er);
    if (!parse(&er)) return fail(er);
    return true;
}

} // namespace Cpm
