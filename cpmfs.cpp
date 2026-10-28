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
#include "cpmfs.h"
#include "diskimage.h"

#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <algorithm>

namespace Cpm {

QString attrString(quint16 a) {
    QStringList l;
    if (a & AttrRO) l << "R/O";
    if (a & AttrSYS) l << "SYS";
    if (a & AttrARC) l << "ARC";
    for (int k = 0; k < 4; ++k) if (a & (AttrF1 << k)) l << QString("F%1").arg(k + 1);
    return l.isEmpty() ? "-" : l.join(' ');
}

// ------------------------------------------------------------------------------------------- stamps

static int fromBcd(quint8 v, bool *ok) {
    if ((v & 0x0F) > 9 || (v >> 4) > 9) *ok = false;
    return (v >> 4) * 10 + (v & 0x0F);
}
static quint8 toBcd(int v) { return quint8((v / 10) << 4 | (v % 10)); }

QDateTime decodeStamp(const QByteArray &b, int off) {
    int days = quint8(b[off]) | quint8(b[off + 1]) << 8;
    if (days == 0) return QDateTime();
    bool ok = true;
    int hh = fromBcd(quint8(b[off + 2]), &ok), mm = fromBcd(quint8(b[off + 3]), &ok);
    if (!ok || hh > 23 || mm > 59) return QDateTime();
    return QDateTime(QDate(1977, 12, 31).addDays(days), QTime(hh, mm));
}

void encodeStamp(QByteArray *b, int off, const QDateTime &t) {
    qint64 days = t.isValid() ? QDate(1977, 12, 31).daysTo(t.date()) : 0;
    if (days < 1 || days > 65535) days = 0;
    (*b)[off] = char(days & 0xFF);
    (*b)[off + 1] = char(days >> 8);
    (*b)[off + 2] = char(days ? toBcd(t.time().hour()) : 0);
    (*b)[off + 3] = char(days ? toBcd(t.time().minute()) : 0);
}

// ------------------------------------------------------------------------------------------- names

static const QString NameChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^`{}~+";

QString Volume::toCpmName(const QString &hostName) {
    QString n = QFileInfo(hostName).fileName().toUpper();
    int dot = n.lastIndexOf('.');
    QString base = dot <= 0 ? n : n.left(dot), ext = dot <= 0 ? QString() : n.mid(dot + 1);
    auto clean = [](const QString &s, int max) {
        QString o;
        for (QChar c : s) {
            if (o.size() >= max) break;
            if (c == ' ') continue;
            o += NameChars.contains(c) ? c : QChar('-');
        }
        return o;
    };
    base = clean(base, 8);
    ext = clean(ext, 3);
    if (base.isEmpty()) base = "FILE";
    return ext.isEmpty() ? base : base + "." + ext;
}

bool Volume::validName(const QString &name, QString *why) {
    int dot = name.indexOf('.');
    QString base = dot < 0 ? name : name.left(dot), ext = dot < 0 ? QString() : name.mid(dot + 1);
    if (base.isEmpty()) { *why = "the name is empty"; return false; }
    if (base.size() > 8) { *why = "the name part is longer than 8 characters"; return false; }
    if (ext.size() > 3 || ext.contains('.')) { *why = "the extension is longer than 3 characters"; return false; }
    for (QChar c : base + ext)
        if (!NameChars.contains(c)) {
            *why = QString("'%1' is not allowed in a CP/M name (use A-Z, 0-9 and %2)").arg(c).arg(NameChars.mid(36));
            return false;
        }
    return true;
}

QByteArray Volume::name11(const QString &name) {
    int dot = name.lastIndexOf('.');
    QByteArray base = (dot < 0 ? name : name.left(dot)).toLatin1().left(8);
    QByteArray ext = (dot < 0 ? QString() : name.mid(dot + 1)).toLatin1().left(3);
    base.append(QByteArray(8 - base.size(), ' '));
    ext.append(QByteArray(3 - ext.size(), ' '));
    return base + ext;
}

static QString displayName(const QByteArray &n11) {
    QString base = QString::fromLatin1(n11.left(8)), ext = QString::fromLatin1(n11.mid(8, 3));
    while (base.endsWith(' ')) base.chop(1);
    while (ext.endsWith(' ')) ext.chop(1);
    return ext.isEmpty() ? base : base + "." + ext;
}

// ------------------------------------------------------------------------------------------- raw access

bool Volume::readBytes(qint64 off, int len, QByteArray *out, QString *err) const {
    out->clear();
    const int sl = m_fmt.seclen;
    qint64 abs = qint64(m_fmt.dataStart) * sl + off;
    QByteArray s;
    while (len > 0) {
        int lsec = int(abs / sl), within = int(abs % sl);
        int c, h, i;
        if (!m_fmt.locate(lsec, &c, &h, &i)) { *err = "read beyond the end of the disc"; return false; }
        if (!m_img->read(c, h, i, &s) || s.size() != sl) {
            *err = QString("cannot read logical sector %1 (cylinder %2 head %3, sector %4 on the track)").arg(lsec).arg(c).arg(h).arg(i + 1);
            return false;
        }
        int n = qMin(len, sl - within);
        out->append(s.constData() + within, n);
        abs += n;
        len -= n;
    }
    return true;
}

bool Volume::writeBytes(qint64 off, const QByteArray &data, QString *err) {
    const int sl = m_fmt.seclen;
    qint64 abs = qint64(m_fmt.dataStart) * sl + off;
    int pos = 0;
    QByteArray s;
    while (pos < data.size()) {
        int lsec = int(abs / sl), within = int(abs % sl);
        int c, h, i;
        if (!m_fmt.locate(lsec, &c, &h, &i)) { *err = "write beyond the end of the disc"; return false; }
        int n = qMin(data.size() - pos, sl - within);
        if (n < sl) {
            if (!m_img->read(c, h, i, &s) || s.size() != sl) { *err = QString("cannot read logical sector %1").arg(lsec); return false; }
        } else {
            s = QByteArray(sl, '\0');
        }
        s.replace(within, n, data.mid(pos, n));
        if (!m_img->write(c, h, i, s, err)) return false;
        abs += n;
        pos += n;
    }
    return true;
}

// ------------------------------------------------------------------------------------------- directory

bool Volume::attach(Disk::Image *img, const Format &f, QString *err) {
    m_img = img;
    m_fmt = f;
    if (!m_fmt.derived && !m_fmt.derive(err)) { m_img = nullptr; return false; }
    if (!parse(err)) { m_img = nullptr; return false; }
    return true;
}

bool Volume::reload(QString *err) {
    QString e;
    bool ok = parse(&e);
    if (!ok && err) *err = e;
    return ok;
}

bool Volume::isFileEntry(quint8 st) const { return st <= 15 || (st <= 31 && m_fmt.os != Os::V3); }

// An entry with a user number but a name no CP/M could have made (control characters) and nothing in it - no
// records, no blocks. Seen as the first entry of a real Acorn Z80 disc (Colossal Cave), perhaps a disc identity
// written by Acorn's tools. It isn't listed as a file, its slot is never reused, and it is left exactly as it is.
bool Volume::inertEntry(const QByteArray &e) const {
    if (!isFileEntry(quint8(e[0])) || quint8(e[15]) != 0) return false;
    for (int k = 16; k < 32; ++k) if (e[k]) return false;
    for (int k = 1; k <= 11; ++k) if ((quint8(e[k]) & 0x7F) < 0x20) return true;
    return false;
}

int Volume::logicalExtent(const QByteArray &e) const { return (quint8(e[14]) & 0x3F) * 32 + (quint8(e[12]) & 0x1F); }

int Volume::recordsIn(const QByteArray &e) const {
    return (quint8(e[12]) & m_fmt.exm) * 128 + qMin(int(quint8(e[15])), 128);
}

QVector<int> Volume::entryBlocks(const QByteArray &e) const {
    QVector<int> b;
    if (m_fmt.ptr16) for (int k = 0; k < 8; ++k) b.append(quint8(e[16 + 2 * k]) | quint8(e[17 + 2 * k]) << 8);
    else for (int k = 0; k < 16; ++k) b.append(quint8(e[16 + k]));
    return b;
}

bool Volume::stampSlot(int entry, QByteArray *sfcb, int *slotOffset) const {
    if (!m_stamps || (entry & 3) == 3) return false;
    int si = entry | 3;
    if (si >= m_dir.size() || quint8(m_dir[si][0]) != 0x21) return false;
    *sfcb = m_dir[si];
    *slotOffset = 1 + 10 * (entry & 3);
    return true;
}

bool Volume::parse(QString *err) {
    m_files.clear();
    m_dir.clear();
    m_label = Label();
    m_stamps = false;
    QByteArray raw;
    if (!readBytes(0, m_fmt.maxdir * 32, &raw, err)) return false;
    for (int i = 0; i < m_fmt.maxdir; ++i) m_dir.append(raw.mid(i * 32, 32));
    for (int i = 3; i < m_dir.size(); i += 4) if (quint8(m_dir[i][0]) == 0x21) m_stamps = true;
    for (int i = 0; i < m_dir.size(); ++i) {
        const QByteArray &e = m_dir[i];
        if (quint8(e[0]) == 0x20 && !m_label.present) {
            m_label.present = true;
            m_label.index = i;
            QByteArray n = e.mid(1, 11);
            for (char &c : n) c = char(c & 0x7F);
            m_label.name = displayName(n);
            m_label.mode = quint8(e[12]);
            m_label.created = decodeStamp(e, 24);
            m_label.updated = decodeStamp(e, 28);
        }
    }
    QMap<QByteArray, File> byKey;
    QMap<QByteArray, QVector<QPair<int, int>>> ext;      // key -> (logical extent, entry)
    for (int i = 0; i < m_dir.size(); ++i) {
        const QByteArray &e = m_dir[i];
        quint8 st = quint8(e[0]);
        if (!isFileEntry(st) || inertEntry(e)) continue;
        QByteArray n = e.mid(1, 11);
        for (char &c : n) c = char(c & 0x7F);
        QByteArray k = QByteArray(1, char(st)) + n;
        if (!byKey.contains(k)) {
            File f;
            f.user = st;
            f.name11 = n;
            f.name = displayName(n);
            byKey[k] = f;
        }
        ext[k].append(qMakePair(logicalExtent(e), i));
    }
    for (auto it = byKey.begin(); it != byKey.end(); ++it) {
        File &f = it.value();
        auto &l = ext[it.key()];
        std::sort(l.begin(), l.end());
        int prev = -1;
        for (const auto &p : l) {
            f.entries.append(p.second);
            if (p.first == prev) f.problem = QString("two directory entries for extent %1").arg(p.first);
            prev = p.first;
        }
        const QByteArray &first = m_dir[l.first().second];
        f.attrs = quint16(((quint8(first[9]) & 0x80) ? AttrRO : 0) | ((quint8(first[10]) & 0x80) ? AttrSYS : 0) |
                          ((quint8(first[11]) & 0x80) ? AttrARC : 0));
        for (int k = 0; k < 4; ++k) if (quint8(first[1 + k]) & 0x80) f.attrs |= quint16(AttrF1 << k);
        qint64 recs = 0;
        for (const auto &p : l) {
            const QByteArray &e = m_dir[p.second];
            qint64 end = qint64(p.first & ~m_fmt.exm) * 128 + recordsIn(e);
            recs = qMax(recs, end);
            for (int b : entryBlocks(e)) if (b) f.blocks++;
        }
        f.records = recs;
        f.size = recs * 128;
        const QByteArray &last = m_dir[l.last().second];
        int bc = quint8(last[13]);
        // The last record's byte count (CP/M 3; cpmtools writes it for every format; CP/M 2.2 leaves it 0)
        if (bc > 0 && bc < 128 && recs > 0) { f.size -= 128 - bc; f.exactSize = true; }
        // a gap between extents (that isn't a whole-extent hole) usually means a lost directory entry
        int expectEntries = int((recs + (m_fmt.exm + 1) * 128 - 1) / ((m_fmt.exm + 1) * 128));
        if (f.problem.isEmpty() && recs > 0 && l.size() < expectEntries)
            f.problem = QString("%1 of %2 directory entries present").arg(l.size()).arg(expectEntries);
        QByteArray sf;
        int so;
        if (stampSlot(l.first().second, &sf, &so)) {
            QDateTime a = decodeStamp(sf, so), u = decodeStamp(sf, so + 4);
            if (m_label.present && (m_label.mode & 0x40)) f.accessed = a; else f.created = a;
            f.updated = u;
        }
        m_files.append(f);
    }
    std::sort(m_files.begin(), m_files.end(), [](const File &a, const File &b) {
        if (a.user != b.user) return a.user < b.user;
        return a.name < b.name;
    });
    return true;
}

int Volume::indexOf(int user, const QString &name) const {
    QByteArray n = name11(name);
    for (int i = 0; i < m_files.size(); ++i)
        if (m_files[i].user == user && m_files[i].name11 == n) return i;
    return -1;
}

QVector<int> Volume::usersInUse() const {
    QVector<int> u;
    for (const File &f : m_files) if (!u.contains(f.user)) u.append(f.user);
    std::sort(u.begin(), u.end());
    return u;
}

bool Volume::readFile(const File &f, QByteArray *out, QString *err) const {
    QByteArray d(int(f.records * 128), '\0');
    for (int ei : f.entries) {
        const QByteArray &e = m_dir[ei];
        qint64 base = qint64(logicalExtent(e) & ~m_fmt.exm) * 16384;
        int bytes = recordsIn(e) * 128;
        QVector<int> bl = entryBlocks(e);
        for (int k = 0; k < bl.size(); ++k) {
            int pos = k * m_fmt.blocksize;
            if (pos >= bytes) break;
            int n = qMin(m_fmt.blocksize, bytes - pos);
            if (bl[k] == 0) continue;                       // a hole: reads as zeros here
            if (bl[k] > m_fmt.dsm) { *err = QString("%1: block %2 is beyond the end of the disc").arg(f.name).arg(bl[k]); return false; }
            QByteArray chunk;
            if (!readBytes(qint64(bl[k]) * m_fmt.blocksize, n, &chunk, err)) { *err = f.name + ": " + *err; return false; }
            if (base + pos + n > d.size()) d.resize(int(base + pos + n));
            d.replace(int(base + pos), n, chunk);
        }
    }
    *out = d.left(int(f.size));
    return true;
}

// ------------------------------------------------------------------------------------------- free space

QVector<bool> Volume::allocation() const {
    QVector<bool> used(m_fmt.dsm + 1, false);
    for (int b = 0; b < m_fmt.dirBlocks && b <= m_fmt.dsm; ++b) used[b] = true;
    for (const QByteArray &e : m_dir) {
        if (!isFileEntry(quint8(e[0]))) continue;
        for (int b : entryBlocks(e)) if (b > 0 && b <= m_fmt.dsm) used[b] = true;
    }
    return used;
}

int Volume::freeBlocks() const {
    int n = 0;
    for (bool u : allocation()) if (!u) ++n;
    return n;
}

bool Volume::freeSlot(int i) const { return quint8(m_dir[i][0]) == 0xE5 && !(m_stamps && (i & 3) == 3); }

int Volume::freeEntries() const {
    int n = 0;
    for (int i = 0; i < m_dir.size(); ++i) if (freeSlot(i)) ++n;
    return n;
}

int Volume::usedEntries() const {
    int n = 0;
    for (const QByteArray &e : m_dir) if (isFileEntry(quint8(e[0]))) ++n;
    return n;
}

bool Volume::labelAllowed() const { return m_label.present || m_fmt.os == Os::V3 || m_fmt.os == Os::Zsys; }

QString Volume::stampKinds() const {
    if (!m_stamps) return QString();
    if (m_fmt.os == Os::P2dos || m_fmt.os == Os::Zsys || !m_label.present) return m_label.present ? "create, update" : "slots present, not enabled";
    QStringList l;
    if (m_label.mode & 0x10) l << "create";
    if (m_label.mode & 0x40) l << "access";
    if (m_label.mode & 0x20) l << "update";
    return l.isEmpty() ? QString("slots present, not enabled") : l.join(", ");
}

// ------------------------------------------------------------------------------------------- check

Report Volume::check() const {
    Report r;
    QMap<int, QString> owner;              // block -> "USER:NAME"
    for (int i = 0; i < m_dir.size(); ++i) {
        const QByteArray &e = m_dir[i];
        quint8 st = quint8(e[0]);
        if (st == 0xE5) continue;
        if (st == 0x21) {
            if ((i & 3) != 3) r.warnings << QString("entry %1: a time stamp entry that is not in a fourth slot").arg(i);
            continue;
        }
        if (st == 0x20) continue;
        if (!isFileEntry(st)) {
            if (st >= 16 && st <= 31 && m_fmt.os == Os::V3) continue;    // CP/M 3 password entry
            r.errors << QString("entry %1: status byte %2 is not a user number, label, stamp or free entry").arg(i).arg(st, 2, 16, QChar('0'));
            continue;
        }
        if (inertEntry(e)) {
            r.warnings << QString("entry %1 is not a file (a name with control characters and no data): left as it is").arg(i);
            continue;
        }
        QByteArray n = e.mid(1, 11), masked = n;
        for (char &c : masked) c = char(c & 0x7F);
        QString shown = QString("%1:%2").arg(st).arg(displayName(masked));
        for (char c : n)
            if ((c & 0x7F) < 0x20) { r.errors << QString("entry %1: the name has control characters").arg(i); break; }
        if (quint8(e[15]) > 0x80) r.errors << QString("entry %1 (%2): record count %3 is more than 128").arg(i).arg(shown).arg(quint8(e[15]));
        if (quint8(e[12]) & 0xE0) r.warnings << QString("entry %1 (%2): extent byte %3 has its top bits set").arg(i).arg(shown).arg(quint8(e[12]), 2, 16, QChar('0'));
        QVector<int> bl = entryBlocks(e);
        int nz = 0;
        for (int b : bl) {
            if (!b) continue;
            ++nz;
            if (b > m_fmt.dsm) { r.errors << QString("entry %1 (%2): block %3 is beyond the end of the disc (%4 blocks)").arg(i).arg(shown).arg(b).arg(m_fmt.dsm + 1); continue; }
            if (b < m_fmt.dirBlocks) { r.errors << QString("entry %1 (%2): block %3 is in the directory").arg(i).arg(shown).arg(b); continue; }
            if (owner.contains(b)) r.errors << QString("block %1 is used by both %2 and %3").arg(b).arg(owner[b], shown);
            else owner[b] = shown;
        }
        int need = (recordsIn(e) * 128 + m_fmt.blocksize - 1) / m_fmt.blocksize;
        if (nz > need) r.warnings << QString("entry %1 (%2): %3 blocks allocated for %4 records").arg(i).arg(shown).arg(nz).arg(recordsIn(e));
        else if (nz < need) r.warnings << QString("entry %1 (%2): %3 records but only %4 blocks (a sparse file?)").arg(i).arg(shown).arg(recordsIn(e)).arg(nz);
    }
    for (const File &f : m_files)
        if (!f.problem.isEmpty()) {
            if (f.problem.startsWith("two")) r.errors << QString("%1:%2: %3").arg(f.user).arg(f.name, f.problem);
            else r.warnings << QString("%1:%2: %3").arg(f.user).arg(f.name, f.problem);
        }
    return r;
}

// ------------------------------------------------------------------------------------------- changes

bool Volume::writeEntry(int index, const QByteArray &e, QString *err) {
    if (!writeBytes(qint64(index) * 32, e.left(32), err)) return false;
    m_dir[index] = e.left(32);
    return true;
}

bool Volume::writeStamps(int entry, const QDateTime &first, const QDateTime &update, QString *err) {
    QByteArray sf;
    int so;
    if (!stampSlot(entry, &sf, &so)) return true;
    bool p2 = m_fmt.os == Os::P2dos || m_fmt.os == Os::Zsys;
    bool doFirst = p2 || (m_label.present && (m_label.mode & 0x50));
    bool doUpdate = p2 || (m_label.present && (m_label.mode & 0x20));
    if (first.isValid() && !doFirst && !doUpdate) return true;
    if (!first.isValid()) {                     // clearing (file deleted)
        for (int k = 0; k < 10; ++k) sf[so + k] = 0;
    } else {
        if (doFirst) encodeStamp(&sf, so, first);
        if (doUpdate) encodeStamp(&sf, so + 4, update);
    }
    return writeEntry(entry | 3, sf, err);
}

#define TXN_BEGIN auto snap__ = m_img->snapshot(); auto fail = [&](const QString &e) { *err = e; m_img->restore(snap__); reload(); return false; };

bool Volume::addFile(int user, const QString &nameIn, const QByteArray &data, quint16 attrs, const QDateTime &when,
                     bool replace, QString *err) {
    QString name = nameIn.toUpper(), why;
    if (!validName(name, &why)) { *err = name + ": " + why; return false; }
    int maxUser = m_fmt.os == Os::V3 ? 15 : 31;
    if (user < 0 || user > maxUser) { *err = QString("user number %1 is out of range (0-%2)").arg(user).arg(maxUser); return false; }
    TXN_BEGIN
    int ix = indexOf(user, name);
    if (ix >= 0) {
        if (!replace) return fail(QString("%1 already exists in user %2").arg(name).arg(user));
        if (m_files[ix].attrs & AttrRO) return fail(QString("%1 is read-only").arg(name));
        QString e;
        if (!deleteFile(user, name, &e)) return fail(e);
    }
    const int bls = m_fmt.blocksize;
    qint64 records = (qint64(data.size()) + 127) / 128;
    int nblocks = int((qint64(data.size()) + bls - 1) / bls);
    qint64 recsPerEntry = qint64(m_fmt.exm + 1) * 128;
    int nentries = qMax(1, int((records + recsPerEntry - 1) / recsPerEntry));
    if (records > 65535LL * 128) return fail(name + ": too large for CP/M");
    QVector<bool> used = allocation();
    QVector<int> freeB;
    for (int b = 0; b < used.size() && freeB.size() < nblocks; ++b) if (!used[b]) freeB.append(b);
    if (freeB.size() < nblocks)
        return fail(QString("%1: the disc is full (%2K needed, %3K free)").arg(name).arg(qint64(nblocks) * bls / 1024).arg(freeBytes() / 1024));
    QVector<int> freeSlotList;
    for (int i = 0; i < m_dir.size() && freeSlotList.size() < nentries; ++i) if (freeSlot(i)) freeSlotList.append(i);
    if (freeSlotList.size() < nentries) return fail(QString("%1: the directory is full").arg(name));

    QByteArray padded = data;
    padded.append(QByteArray(int(records * 128 - data.size()), char(0x1A)));
    for (int j = 0; j < nblocks; ++j) {
        QByteArray chunk = padded.mid(j * bls, bls);
        chunk.append(QByteArray(bls - chunk.size(), char(0xE5)));
        QString e;
        if (!writeBytes(qint64(freeB[j]) * bls, chunk, &e)) return fail(e);
    }
    QByteArray n11 = name11(name);
    for (int k = 0; k < nentries; ++k) {
        QByteArray e(32, '\0');
        e[0] = char(user);
        for (int c = 0; c < 11; ++c) e[1 + c] = n11[c];
        for (int b = 0; b < 4; ++b) if (attrs & (AttrF1 << b)) e[1 + b] = char(e[1 + b] | 0x80);
        if (attrs & AttrRO) e[9] = char(e[9] | 0x80);
        if (attrs & AttrSYS) e[10] = char(e[10] | 0x80);
        if (attrs & AttrARC) e[11] = char(e[11] | 0x80);
        qint64 recsHere = qMin(recsPerEntry, records - k * recsPerEntry);
        int lext;
        int rc;
        if (recsHere <= 0) { lext = k * (m_fmt.exm + 1); rc = 0; }
        else {
            int within = int((recsHere - 1) / 128);
            lext = k * (m_fmt.exm + 1) + within;
            rc = int(recsHere - qint64(within) * 128);
        }
        e[12] = char(lext & 0x1F);
        e[14] = char(lext >> 5);
        e[15] = char(rc);
        if (k == nentries - 1) e[13] = char(data.size() % 128);    // exact size, as CP/M 3 and cpmtools record it
        int perEntry = m_fmt.ptrsPerEntry;
        for (int p = 0; p < perEntry; ++p) {
            int bi = k * perEntry + p;
            if (bi >= nblocks) break;
            int b = freeB[bi];
            if (m_fmt.ptr16) { e[16 + 2 * p] = char(b & 0xFF); e[17 + 2 * p] = char(b >> 8); }
            else e[16 + p] = char(b);
        }
        QString er;
        if (!writeEntry(freeSlotList[k], e, &er)) return fail(er);
    }
    QDateTime t = when.isValid() ? when : QDateTime::currentDateTime();
    QString er;
    if (!writeStamps(freeSlotList[0], t, t, &er)) return fail(er);
    reload();
    return true;
}

bool Volume::deleteFile(int user, const QString &name, QString *err) {
    int ix = indexOf(user, name);
    if (ix < 0) { *err = QString("%1 is not in user %2").arg(name).arg(user); return false; }
    TXN_BEGIN
    File f = m_files[ix];
    for (int ei : f.entries) {
        QByteArray e = m_dir[ei];
        e[0] = char(0xE5);
        QString er;
        if (!writeEntry(ei, e, &er)) return fail(er);
        if (!writeStamps(ei, QDateTime(), QDateTime(), &er)) return fail(er);
    }
    // CP/M 3 password entries for the same name go too
    for (int i = 0; i < m_dir.size(); ++i) {
        QByteArray e = m_dir[i];
        quint8 st = quint8(e[0]);
        if (m_fmt.os != Os::V3 || st != quint8(user + 16)) continue;
        QByteArray n = e.mid(1, 11);
        for (char &c : n) c = char(c & 0x7F);
        if (n != f.name11) continue;
        e[0] = char(0xE5);
        QString er;
        if (!writeEntry(i, e, &er)) return fail(er);
    }
    reload();
    return true;
}

bool Volume::rename(int user, const QString &name, int newUser, const QString &newNameIn, QString *err) {
    int ix = indexOf(user, name);
    if (ix < 0) { *err = QString("%1 is not in user %2").arg(name).arg(user); return false; }
    QString newName = newNameIn.toUpper(), why;
    if (!validName(newName, &why)) { *err = newName + ": " + why; return false; }
    int maxUser = m_fmt.os == Os::V3 ? 15 : 31;
    if (newUser < 0 || newUser > maxUser) { *err = QString("user number %1 is out of range (0-%2)").arg(newUser).arg(maxUser); return false; }
    int clash = indexOf(newUser, newName);
    if (clash >= 0 && clash != ix) { *err = QString("%1 already exists in user %2").arg(newName).arg(newUser); return false; }
    TXN_BEGIN
    QByteArray n11 = name11(newName);
    File f = m_files[ix];
    for (int ei : f.entries) {
        QByteArray e = m_dir[ei];
        e[0] = char(newUser);
        for (int c = 0; c < 11; ++c) e[1 + c] = char((quint8(e[1 + c]) & 0x80) | quint8(n11[c]));
        QString er;
        if (!writeEntry(ei, e, &er)) return fail(er);
    }
    reload();
    return true;
}

bool Volume::setAttrs(int user, const QString &name, quint16 attrs, QString *err) {
    int ix = indexOf(user, name);
    if (ix < 0) { *err = QString("%1 is not in user %2").arg(name).arg(user); return false; }
    TXN_BEGIN
    File f = m_files[ix];
    for (int ei : f.entries) {
        QByteArray e = m_dir[ei];
        auto setBit = [&e](int at, bool on) { e[at] = char(on ? (quint8(e[at]) | 0x80) : (quint8(e[at]) & 0x7F)); };
        for (int b = 0; b < 4; ++b) setBit(1 + b, attrs & (AttrF1 << b));
        setBit(9, attrs & AttrRO);
        setBit(10, attrs & AttrSYS);
        setBit(11, attrs & AttrARC);
        QString er;
        if (!writeEntry(ei, e, &er)) return fail(er);
    }
    reload();
    return true;
}

bool Volume::setLabel(const QString &labelIn, QString *err) {
    if (!labelAllowed()) { *err = "disc labels are a CP/M 3 feature; this disc's format is CP/M 2.2"; return false; }
    QString label = labelIn.toUpper().trimmed(), why;
    if (!label.isEmpty() && !validName(label, &why)) { *err = label + ": " + why; return false; }
    TXN_BEGIN
    QByteArray n11 = name11(label.isEmpty() ? QString() : label);
    QString er;
    if (m_label.present) {
        QByteArray e = m_dir[m_label.index];
        for (int c = 0; c < 11; ++c) e[1 + c] = n11[c];
        encodeStamp(&e, 28, QDateTime::currentDateTime());
        if (!writeEntry(m_label.index, e, &er)) return fail(er);
    } else {
        int slot = -1;
        for (int i = 0; i < m_dir.size() && slot < 0; ++i) if (freeSlot(i)) slot = i;
        if (slot < 0) return fail("the directory is full");
        QByteArray e(32, '\0');
        e[0] = char(0x20);
        for (int c = 0; c < 11; ++c) e[1 + c] = n11[c];
        e[12] = char(0x01);
        encodeStamp(&e, 24, QDateTime::currentDateTime());
        encodeStamp(&e, 28, QDateTime::currentDateTime());
        if (!writeEntry(slot, e, &er)) return fail(er);
    }
    reload();
    return true;
}

bool Volume::initDirectory(bool stamps, const QString &labelName, QString *err) {
    TXN_BEGIN
    QString er;
    for (int i = 0; i < m_fmt.maxdir; ++i) {
        QByteArray e(32, char(0xE5));
        if (stamps && (i & 3) == 3) { e = QByteArray(32, '\0'); e[0] = char(0x21); }
        if (stamps && i == 0) {
            e = QByteArray(32, '\0');
            e[0] = char(0x20);
            QByteArray n11 = name11(labelName.toUpper());
            for (int c = 0; c < 11; ++c) e[1 + c] = n11[c];
            e[12] = char(0x01 | 0x10 | 0x20);          // label exists, create and update stamps
            encodeStamp(&e, 24, QDateTime::currentDateTime());
            encodeStamp(&e, 28, QDateTime::currentDateTime());
        }
        if (!writeBytes(qint64(i) * 32, e, &er)) return fail(er);
    }
    if (!parse(&er)) return fail(er);
    return true;
}

// ------------------------------------------------------------------------------------------- new discs

int defaultImdMode(const Format &f) {
    if (f.imdMode >= 0) return f.imdMode;
    int trackBytes = f.seclen * f.sectrk;
    bool eightInch = (f.sectrk == 26 && f.seclen <= 256) || trackBytes >= 7680;    // 8" or high density
    if (f.fm || f.datarate == "SD") return eightInch ? 0 : 2;
    return eightInch ? 3 : 5;
}

bool makeBlank(Disk::Image *img, Disk::Kind kind, const Format &fIn, bool stamps, const QString &label, QString *err) {
    Format f = fIn;
    if (!f.derived && !f.derive(err)) return false;
    if (f.offsetSectors) { *err = "formats with an image offset (hard disc partitions) can't be made as new discs"; return false; }
    Disk::RawLayout l;
    l.seclen = f.seclen;
    l.sectrk = f.sectrk;
    l.heads = f.physHeads;
    l.cyls = f.cylinders;
    l.firstId = f.secbase >= 0 ? f.secbase : 1;
    l.mode = defaultImdMode(f);
    if (f.t0sectrk > 0 && kind == Disk::Kind::Raw) {
        *err = QString("%1 has a track 0 of its own shape (%2 x %3 %4), which a raw image can't hold: make it as ImageDisk or DSK")
                   .arg(f.name).arg(f.t0sectrk).arg(f.t0seclen).arg(f.t0fm ? "FM" : "MFM");
        return false;
    }
    img->createBlank(kind, l, QString("Made by CP/M Disk Wrangler, format %1").arg(f.name));
    if (f.t0sectrk > 0) {
        // the same data rate as the other tracks, in FM or MFM (ImageDisk modes 0-2 FM, 3-5 MFM)
        int rate = l.mode % 3;
        int mode = f.t0fm ? rate : 3 + rate;
        int first = f.t0firstId >= 0 ? f.t0firstId : l.firstId;
        if (!img->reshapeTrack(0, 0, f.t0sectrk, f.t0seclen, first, mode, err)) return false;
    }
    Volume v;
    // attach reads an all-E5 directory; then write the empty directory properly (stamps, label)
    if (!v.attach(img, f, err)) return false;
    if (stamps && !v.initDirectory(true, label, err)) return false;
    if (f.acornDfs) {
        QByteArray cat = acornDfsCatalogue(f.cylinders * f.sectrk);
        if (!img->write(0, 0, 0, cat.left(256), err) || !img->write(0, 0, 1, cat.mid(256), err)) return false;
    }
    img->markForSave();
    return true;
}

} // namespace Cpm
