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
#include "diskimage.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>

namespace Disk {

QString kindName(Kind k) {
    switch (k) {
    case Kind::Raw: return "raw sector image";
    case Kind::Imd: return "ImageDisk (.IMD)";
    case Kind::Dsk: return "CPCEMU disc (.DSK)";
    case Kind::Edsk: return "extended CPCEMU disc (.DSK)";
    case Kind::Td0: return "TeleDisk (.TD0)";
    default: return "none";
    }
}

QString kindExtension(Kind k) {
    switch (k) {
    case Kind::Raw: return "img";
    case Kind::Imd: return "imd";
    case Kind::Dsk: case Kind::Edsk: return "dsk";
    case Kind::Td0: return "td0";
    default: return QString();
    }
}

static int u16(const QByteArray &b, int o) { return quint8(b[o]) | quint8(b[o + 1]) << 8; }
static int sizeCode(int size) {
    for (int n = 0; n <= 7; ++n) if ((128 << n) == size) return n;
    return -1;
}

// ------------------------------------------------------------------------------------------- opening

void Image::close() { *this = Image(); }

bool Image::open(const QString &path, QString *err) {
    close();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *err = f.errorString(); return false; }
    QByteArray b = f.readAll();
    f.close();
    m_path = path;
    bool ok;
    if (b.startsWith("IMD ")) {
        m_kind = Kind::Imd;
        ok = parseImd(b, err);
    } else if (b.startsWith("EXTENDED CPC DSK File")) {
        m_kind = Kind::Edsk;
        ok = parseDsk(b, err);
    } else if (b.startsWith("MV - CPC")) {
        m_kind = Kind::Dsk;
        ok = parseDsk(b, err);
    } else if (b.size() >= 12 && ((b[0] == 'T' && b[1] == 'D') || (b[0] == 't' && b[1] == 'd')) && b[2] == char(0) &&
               quint8(b[9]) <= 2) {
        m_kind = Kind::Td0;
        ok = parseTd0(b, err);
    } else {
        if (b.isEmpty()) { *err = "the file is empty"; return false; }
        m_kind = Kind::Raw;
        m_raw = b;
        m_rawFileSize = b.size();
        m_file = b;
        ok = true;
    }
    if (!ok) { Kind k = m_kind; close(); m_kind = Kind::None; Q_UNUSED(k); return false; }
    if (m_kind != Kind::Raw) { m_file = b; index(); }
    return true;
}

void Image::createBlank(Kind kind, const RawLayout &l, const QString &comment) {
    close();
    m_kind = kind;
    m_comment = comment;
    m_layout = l;
    for (int c = 0; c < l.cyls; ++c)
        for (int h = 0; h < l.heads; ++h) {
            Track t;
            t.cyl = c;
            t.head = h;
            t.mode = l.mode;
            for (int s = 0; s < l.sectrk; ++s) {
                Sector x;
                x.c = quint8(c); x.h = quint8(h); x.r = quint8(l.firstId + s); x.n = quint8(qMax(0, sizeCode(l.seclen)));
                x.size = l.seclen;
                x.data = QByteArray(l.seclen, char(0xE5));
                x.imdType = 2;
                t.secs.append(x);
            }
            m_tracks.append(t);
        }
    if (kind == Kind::Raw) {
        m_raw = QByteArray(int(l.bytes()), char(0xE5));
        m_rawFileSize = m_raw.size();
        QString e;
        setRawLayout(l, &e);
    } else {
        index();
    }
    m_forceSave = true;
}

bool Image::setRawLayout(const RawLayout &l, QString *err) {
    if (m_kind != Kind::Raw) { *err = "not a raw image"; return false; }
    if (!l.valid()) { *err = "invalid geometry"; return false; }
    if (l.bytes() > 512LL * 1024 * 1024) { *err = "geometry too large"; return false; }
    m_layout = l;
    QByteArray b = m_raw.left(int(m_rawFileSize));
    if (b.size() < l.bytes()) b.append(QByteArray(int(l.bytes() - b.size()), char(0xE5)));
    m_raw = b;
    m_tracks.clear();
    int sc = sizeCode(l.seclen);
    for (int c = 0; c < l.cyls; ++c)
        for (int h = 0; h < l.heads; ++h) {
            Track t;
            t.cyl = c;
            t.head = h;
            t.mode = l.mode;
            for (int s = 0; s < l.sectrk; ++s) {
                Sector x;
                x.c = quint8(c); x.h = quint8(h); x.r = quint8(l.firstId + s); x.n = quint8(qMax(0, sc));
                x.size = l.seclen;
                x.fileOffset = int(((qint64(c) * l.heads + h) * l.sectrk + s) * l.seclen);
                x.data = m_raw.mid(x.fileOffset, l.seclen);
                t.secs.append(x);
            }
            m_tracks.append(t);
        }
    index();
    return true;
}

void Image::index() {
    m_trackIndex.clear();
    m_cyls = m_heads = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
        Track &t = m_tracks[i];
        m_trackIndex[key(t.cyl, t.head, 0)] = i;
        m_cyls = qMax(m_cyls, t.cyl + 1);
        m_heads = qMax(m_heads, t.head + 1);
        t.byId.clear();
        for (int s = 0; s < t.secs.size(); ++s) t.byId.append(s);
        std::stable_sort(t.byId.begin(), t.byId.end(), [&t](int a, int b) { return t.secs[a].r < t.secs[b].r; });
        t.duplicateIds = false;
        for (int s = 1; s < t.byId.size(); ++s)
            if (t.secs[t.byId[s]].r == t.secs[t.byId[s - 1]].r) t.duplicateIds = true;
    }
}

// ------------------------------------------------------------------------------------------- ImageDisk

bool Image::parseImd(const QByteArray &b, QString *err) {
    int i = b.indexOf(char(0x1A));
    if (i < 0) { *err = "ImageDisk header has no end marker"; return false; }
    QByteArray hdr = b.left(i);
    int nl = hdr.indexOf('\n');
    m_imdHeader = QString::fromLatin1(nl < 0 ? hdr : hdr.left(nl)).trimmed();
    m_comment = nl < 0 ? QString() : QString::fromLatin1(hdr.mid(nl + 1)).replace("\r\n", "\n").trimmed();
    ++i;
    while (i < b.size()) {
        int start = i;
        if (i + 5 > b.size()) { *err = QString("ImageDisk file ends inside a track header at offset %1").arg(i); return false; }
        Track t;
        t.mode = quint8(b[i]);
        t.cyl = quint8(b[i + 1]);
        int head = quint8(b[i + 2]);
        int ns = quint8(b[i + 3]);
        int sz = quint8(b[i + 4]);
        i += 5;
        if (t.mode > 5 || (head & 0x3F) > 1 || (sz > 6 && sz != 0xFF)) {
            *err = QString("ImageDisk track header at offset %1 is not valid").arg(start);
            return false;
        }
        t.head = head & 0x3F;
        t.imdHeadFlags = head & 0xC0;
        if (i + ns > b.size()) { *err = "ImageDisk file is truncated (sector map)"; return false; }
        QByteArray smap = b.mid(i, ns), cmap, hmap;
        i += ns;
        if (head & 0x80) { cmap = b.mid(i, ns); i += ns; }
        if (head & 0x40) { hmap = b.mid(i, ns); i += ns; }
        QVector<int> sizes(ns, 128 << (sz == 0xFF ? 0 : sz));
        if (sz == 0xFF) {
            for (int k = 0; k < ns; ++k) sizes[k] = u16(b, i + 2 * k);
            i += 2 * ns;
        }
        if (i > b.size()) { *err = "ImageDisk file is truncated (maps)"; return false; }
        for (int k = 0; k < ns; ++k) {
            if (i >= b.size()) { *err = QString("ImageDisk file is truncated in cylinder %1 head %2").arg(t.cyl).arg(t.head); return false; }
            Sector s;
            s.r = quint8(smap[k]);
            s.c = cmap.isEmpty() ? quint8(t.cyl) : quint8(cmap[k]);
            s.h = hmap.isEmpty() ? quint8(t.head) : quint8(hmap[k]);
            s.size = sizes[k];
            s.n = quint8(qMax(0, sizeCode(s.size)));
            int type = quint8(b[i++]);
            s.imdType = quint8(type);
            if (type > 8) { *err = QString("ImageDisk sector record type %1 is not valid").arg(type); return false; }
            if (type == 0) {
                s.missing = true;
            } else if (type % 2 == 0) {                         // compressed: one byte fills the sector
                if (i >= b.size()) { *err = "ImageDisk file is truncated (sector)"; return false; }
                s.data = QByteArray(s.size, b[i++]);
            } else {
                if (i + s.size > b.size()) { *err = "ImageDisk file is truncated (sector data)"; return false; }
                s.data = b.mid(i, s.size);
                i += s.size;
            }
            s.deleted = type == 3 || type == 4 || type == 7 || type == 8;
            s.crcError = type >= 5;
            t.secs.append(s);
        }
        t.imdRaw = b.mid(start, i - start);
        m_tracks.append(t);
    }
    return true;
}

bool Image::writeImd(QByteArray *out, QString *err) const {
    Q_UNUSED(err);
    QByteArray o;
    if (m_kind == Kind::Imd && !m_file.isEmpty()) {
        o = m_file.left(m_file.indexOf(char(0x1A)) + 1);       // header and comment exactly as they were
    } else {
        QDateTime now = QDateTime::currentDateTime();
        o = "IMD 1.18: " + now.toString("dd/MM/yyyy HH:mm:ss").toLatin1() + "\r\n";
        if (!m_comment.isEmpty()) o += m_comment.toLatin1().replace("\n", "\r\n") + "\r\n";
        o += char(0x1A);
    }
    for (int ti = 0; ti < m_tracks.size(); ++ti) {
        const Track &t = m_tracks[ti];
        bool changed = false;
        for (int x = 0; x < t.secs.size() && !changed; ++x)
            if (m_pending.contains(key(t.cyl, t.head, t.byId.indexOf(x)))) changed = true;
        if (!changed && m_kind == Kind::Imd && !t.imdRaw.isEmpty()) { o += t.imdRaw; continue; }
        bool sameSize = true;
        for (const Sector &s : t.secs) if (s.size != t.secs[0].size || sizeCode(s.size) < 0 || sizeCode(s.size) > 6) sameSize = false;
        bool cmap = false, hmap = false;
        for (const Sector &s : t.secs) {
            if (s.c != t.cyl) cmap = true;
            if (s.h != t.head) hmap = true;
        }
        int mode = t.mode >= 0 ? t.mode : (m_layout.mode >= 0 ? m_layout.mode : 5);
        o += char(mode);
        o += char(t.cyl);
        o += char(t.head | (cmap ? 0x80 : 0) | (hmap ? 0x40 : 0));
        o += char(t.secs.size());
        o += char(t.secs.isEmpty() ? 0 : sameSize ? sizeCode(t.secs[0].size) : 0xFF);
        for (const Sector &s : t.secs) o += char(s.r);
        if (cmap) for (const Sector &s : t.secs) o += char(s.c);
        if (hmap) for (const Sector &s : t.secs) o += char(s.h);
        if (!sameSize) for (const Sector &s : t.secs) { o += char(s.size & 0xFF); o += char(s.size >> 8); }
        for (int x = 0; x < t.secs.size(); ++x) {
            const Sector &s = t.secs[x];
            if (s.missing || s.data.isEmpty()) { o += char(0); continue; }
            QByteArray d = sectorData(ti, x);
            int base = s.crcError ? (s.deleted ? 7 : 5) : (s.deleted ? 3 : 1);
            bool uniform = d.count(d[0]) == d.size();
            if (uniform) { o += char(base + 1); o += d[0]; }
            else { o += char(base); o += d; }
        }
    }
    *out = o;
    return true;
}

// ------------------------------------------------------------------------------------------- CPCEMU DSK

bool Image::parseDsk(const QByteArray &b, QString *err) {
    if (b.size() < 256) { *err = "DSK file is shorter than its header"; return false; }
    bool ext = m_kind == Kind::Edsk;
    m_comment = QString::fromLatin1(b.mid(0x22, 14)).trimmed().remove(QChar(0));
    int tracks = quint8(b[0x30]), sides = quint8(b[0x31]);
    if (sides < 1 || sides > 2 || tracks < 1) { *err = "DSK header has an impossible track or side count"; return false; }
    int stdSize = u16(b, 0x32);
    int off = 0x100;
    for (int c = 0; c < tracks; ++c)
        for (int h = 0; h < sides; ++h) {
            int tsize = ext ? quint8(b[0x34 + c * sides + h]) * 256 : stdSize;
            if (tsize == 0) continue;                      // EDSK: unformatted track
            if (off + 0x18 > b.size()) {
                if (ext) { *err = QString("DSK file is truncated at cylinder %1").arg(c); return false; }
                c = tracks; break;                          // standard DSK files are sometimes short at the end
            }
            if (!b.mid(off, 10).startsWith("Track-Info")) {
                *err = QString("DSK track block for cylinder %1 head %2 has no Track-Info header").arg(c).arg(h);
                return false;
            }
            Track t;
            t.cyl = c;
            t.head = h;
            int tn = quint8(b[off + 0x14]);
            int ns = quint8(b[off + 0x15]);
            t.dskGap = quint8(b[off + 0x16]);
            t.dskFill = quint8(b[off + 0x17]);
            int hdrLen = qMax(0x100, ((0x18 + 8 * ns) + 255) / 256 * 256);
            int data = off + hdrLen;
            for (int k = 0; k < ns; ++k) {
                int si = off + 0x18 + 8 * k;
                Sector s;
                s.c = quint8(b[si]); s.h = quint8(b[si + 1]); s.r = quint8(b[si + 2]); s.n = quint8(b[si + 3]);
                s.st1 = quint8(b[si + 4]); s.st2 = quint8(b[si + 5]);
                int nominal = 128 << qMin(int(ext ? s.n : tn), 6);
                int stored = ext ? u16(b, si + 6) : nominal;
                s.size = nominal;
                s.fileOffset = data;
                if (stored == 0 || (s.st1 & 0x01) || (s.st2 & 0x01)) {
                    s.missing = true;
                } else if (data + qMin(stored, nominal) <= b.size()) {
                    s.data = b.mid(data, qMin(stored, nominal));
                    if (stored != nominal) s.crcError = true;   // weak (several copies) or short: never written
                } else {
                    s.missing = true;
                }
                s.deleted = (s.st2 & 0x40) != 0;
                if ((s.st1 & 0x20) || (s.st2 & 0x20)) s.crcError = true;
                data += stored;
                t.secs.append(s);
            }
            m_tracks.append(t);
            off += tsize;
        }
    return true;
}

bool Image::writeDsk(QByteArray *out, bool extended, QString *err) const {
    if (((m_kind == Kind::Dsk && !extended) || (m_kind == Kind::Edsk && extended)) && !m_file.isEmpty()) {
        QByteArray o = m_file;                              // patch the data in place: nothing else changes
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            int c = int(it.key() >> 16), h = int((it.key() >> 8) & 0xFF), i = int(it.key() & 0xFF);
            const Sector *s = sector(c, h, i);
            if (!s || s->fileOffset < 0 || s->fileOffset + s->size > o.size()) { *err = "internal: sector position lost"; return false; }
            o.replace(s->fileOffset, s->size, it.value().left(s->size));
        }
        *out = o;
        return true;
    }
    // a new extended DSK
    int cyls = m_cyls, heads = qMax(1, m_heads);
    QByteArray o(256, '\0');
    QByteArray sig = "EXTENDED CPC DSK File\r\nDisk-Info\r\n";
    o.replace(0, sig.size(), sig);
    QByteArray creator = "CPMDiskWrangle";
    o.replace(0x22, creator.size(), creator);
    o[0x30] = char(cyls);
    o[0x31] = char(heads);
    if (cyls * heads > 204) { *err = "too many tracks for a DSK file"; return false; }
    for (int c = 0; c < cyls; ++c)
        for (int h = 0; h < heads; ++h) {
            const Track *t = track(c, h);
            int ti = t ? int(t - m_tracks.constData()) : -1;
            if (!t || t->secs.isEmpty()) { o[0x34 + c * heads + h] = 0; continue; }
            int ns = t->secs.size();
            int hdrLen = qMax(0x100, ((0x18 + 8 * ns) + 255) / 256 * 256);
            QByteArray tb(hdrLen, '\0');
            QByteArray ti_sig = "Track-Info\r\n";
            tb.replace(0, ti_sig.size(), ti_sig);
            tb[0x10] = char(c);
            tb[0x11] = char(h);
            tb[0x14] = char(t->secs[0].n);
            tb[0x15] = char(ns);
            tb[0x16] = char(t->dskGap);
            tb[0x17] = char(t->dskFill);
            QByteArray data;
            for (int k = 0; k < ns; ++k) {
                const Sector &s = t->secs[k];
                QByteArray d = s.missing ? QByteArray() : sectorData(ti, k);
                int si = 0x18 + 8 * k;
                tb[si] = char(s.c); tb[si + 1] = char(s.h); tb[si + 2] = char(s.r); tb[si + 3] = char(s.n);
                quint8 st1 = s.st1, st2 = s.st2;
                if (s.crcError) { st1 |= 0x20; st2 |= 0x20; }
                if (s.deleted) st2 |= 0x40;
                if (s.missing) st2 |= 0x01;
                tb[si + 4] = char(st1); tb[si + 5] = char(st2);
                tb[si + 6] = char(d.size() & 0xFF); tb[si + 7] = char(d.size() >> 8);
                data += d;
            }
            tb += data;
            int padded = (tb.size() + 255) / 256 * 256;
            if (padded / 256 > 255) { *err = "a track is too large for a DSK file"; return false; }
            tb.append(QByteArray(padded - tb.size(), '\0'));
            o[0x34 + c * heads + h] = char(padded / 256);
            o += tb;
        }
    *out = o;
    return true;
}

// ------------------------------------------------------------------------------------------- raw

bool Image::writeRaw(QByteArray *out, QString *err) const {
    if (m_kind == Kind::Raw) {
        QByteArray o = m_raw;
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            int c = int(it.key() >> 16), h = int((it.key() >> 8) & 0xFF), i = int(it.key() & 0xFF);
            const Sector *s = sector(c, h, i);
            if (!s || s->fileOffset < 0) { *err = "internal: sector position lost"; return false; }
            o.replace(s->fileOffset, s->size, it.value().left(s->size));
        }
        *out = o;
        return true;
    }
    Shape sh = shape();
    if (sh.oddTracks || sh.uniformTracks != m_cyls * m_heads || sh.badSectors) {
        *err = "the disc has tracks of different shapes, missing tracks or bad sectors, which a raw image can't hold";
        return false;
    }
    QByteArray o;
    for (int c = 0; c < m_cyls; ++c)
        for (int h = 0; h < m_heads; ++h) {
            const Track *t = track(c, h);
            for (int i = 0; i < t->byId.size(); ++i) o += sectorData(int(t - m_tracks.constData()), t->byId[i]);
        }
    *out = o;
    return true;
}

// ------------------------------------------------------------------------------------------- access

const Track *Image::track(int cyl, int head) const {
    auto it = m_trackIndex.find(key(cyl, head, 0));
    return it == m_trackIndex.end() ? nullptr : &m_tracks[it.value()];
}

int Image::sectorCount(int cyl, int head) const {
    const Track *t = track(cyl, head);
    return t ? t->secs.size() : 0;
}

const Sector *Image::sector(int cyl, int head, int idx) const {
    const Track *t = track(cyl, head);
    if (!t || idx < 0 || idx >= t->byId.size()) return nullptr;
    return &t->secs[t->byId[idx]];
}

bool Image::sectorUsable(int cyl, int head, int idx) const {
    const Track *t = track(cyl, head);
    const Sector *s = sector(cyl, head, idx);
    return s && !s->missing && !s->crcError && s->data.size() == s->size && !t->duplicateIds;
}

QByteArray Image::sectorData(int ti, int si) const {
    const Track &t = m_tracks[ti];
    int idx = t.byId.indexOf(si);
    auto it = m_pending.find(key(t.cyl, t.head, idx));
    return it != m_pending.end() ? it.value() : t.secs[si].data;
}

bool Image::read(int cyl, int head, int idx, QByteArray *out) const {
    auto it = m_pending.find(key(cyl, head, idx));
    if (it != m_pending.end()) { *out = it.value(); return true; }
    const Sector *s = sector(cyl, head, idx);
    if (!s || s->missing || s->data.isEmpty()) return false;
    *out = s->data;
    return true;
}

bool Image::write(int cyl, int head, int idx, const QByteArray &data, QString *err) {
    const Sector *s = sector(cyl, head, idx);
    if (!s) { *err = QString("cylinder %1 head %2 has no sector %3").arg(cyl).arg(head).arg(idx); return false; }
    if (!sectorUsable(cyl, head, idx)) {
        *err = QString("sector ID %1 on cylinder %2 head %3 is bad or protected and can't be written").arg(s->r).arg(cyl).arg(head);
        return false;
    }
    QByteArray d = data.left(s->size);
    if (d.size() < s->size) d.append(QByteArray(s->size - d.size(), char(0xE5)));
    if (d == s->data) m_pending.remove(key(cyl, head, idx));      // back to what the file holds
    else m_pending[key(cyl, head, idx)] = d;
    return true;
}

Shape Image::shape() const {
    Shape sh;
    sh.allHeads = m_heads;
    QMap<quint64, int> counts;                         // (sectrk, seclen) -> tracks
    for (const Track &t : m_tracks) {
        if (t.secs.isEmpty()) continue;
        bool same = true;
        for (const Sector &s : t.secs) if (s.size != t.secs[0].size) same = false;
        if (!same) continue;
        counts[quint64(t.secs.size()) << 32 | quint64(t.secs[0].size)]++;
    }
    quint64 best = 0;
    int bestN = -1;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        int bytes = it.value() * int(it.key() >> 32) * int(it.key() & 0xFFFFFFFF);
        if (bytes > bestN) { bestN = bytes; best = it.key(); }
    }
    sh.sectrk = int(best >> 32);
    sh.seclen = int(best & 0xFFFFFFFF);
    sh.firstId = 256;
    for (const Track &t : m_tracks) {
        bool common = t.secs.size() == sh.sectrk && !t.secs.isEmpty();
        for (const Sector &s : t.secs) if (s.size != sh.seclen) common = false;
        if (common) {
            sh.uniformTracks++;
            sh.heads = qMax(sh.heads, t.head + 1);
            sh.cyls = qMax(sh.cyls, t.cyl + 1);
            for (const Sector &s : t.secs) sh.firstId = qMin(sh.firstId, int(s.r));
        } else {
            sh.oddTracks++;
        }
        for (const Sector &s : t.secs) if (s.missing || s.crcError) sh.badSectors++;
        if (t.duplicateIds) sh.badSectors++;
    }
    if (sh.firstId == 256) sh.firstId = 0;
    if (const Track *t0 = track(0, 0))
        for (int i : t0->byId) sh.idsOfFirstTrack.append(t0->secs[i].r);
    return sh;
}

// ------------------------------------------------------------------------------------------- saving

bool Image::serialize(Kind as, QByteArray *out, QString *err) const {
    switch (as) {
    case Kind::Imd: return writeImd(out, err);
    case Kind::Dsk: return writeDsk(out, false, err);
    case Kind::Edsk: return writeDsk(out, true, err);
    case Kind::Raw: return writeRaw(out, err);
    case Kind::Td0: *err = "TeleDisk images can't be written; use Save As to save as ImageDisk, DSK or raw"; return false;
    default: *err = "no image"; return false;
    }
}

static bool writeFileAtomically(const QString &path, const QByteArray &bytes, bool backup, QString *err) {
    if (backup && QFileInfo::exists(path)) {
        QString bak = path + ".bak";
        QFile::remove(bak);
        if (!QFile::copy(path, bak)) { *err = "cannot write the backup " + bak; return false; }
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { *err = f.errorString(); return false; }
    if (f.write(bytes) != bytes.size()) { *err = f.errorString(); f.cancelWriting(); return false; }
    if (!f.commit()) { *err = f.errorString(); return false; }
    return true;
}

void Image::applyPending() {
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        int c = int(it.key() >> 16), h = int((it.key() >> 8) & 0xFF), i = int(it.key() & 0xFF);
        auto ti = m_trackIndex.find(key(c, h, 0));
        if (ti == m_trackIndex.end()) continue;
        Track &t = m_tracks[ti.value()];
        if (i < t.byId.size()) {
            Sector &s = t.secs[t.byId[i]];
            s.data = it.value();
        }
        t.imdRaw.clear();
    }
    m_pending.clear();
    m_forceSave = false;
}

bool Image::commit(QString *err, bool backup) {
    if (m_kind == Kind::Td0) { *err = "TeleDisk images are read-only here: use Save As"; return false; }
    if (m_path.isEmpty()) { *err = "the image has no file yet: use Save As"; return false; }
    QByteArray bytes;
    if (!serialize(m_kind, &bytes, err)) return false;
    if (!writeFileAtomically(m_path, bytes, backup, err)) return false;
    // reload what was written so the in-memory state is exactly the file
    QString path = m_path;
    RawLayout l = m_layout;
    Kind k = m_kind;
    Image fresh;
    if (!fresh.open(path, err)) return false;
    if (k == Kind::Raw && l.valid() && !fresh.setRawLayout(l, err)) return false;
    *this = fresh;
    return true;
}

bool Image::saveAs(const QString &path, Kind as, QString *err) {
    QByteArray bytes;
    if (!serialize(as, &bytes, err)) return false;
    if (!writeFileAtomically(path, bytes, false, err)) return false;
    RawLayout l = m_layout;
    if (as == Kind::Raw && m_kind != Kind::Raw) {
        Shape sh = shape();
        l.seclen = sh.seclen; l.sectrk = sh.sectrk; l.cyls = m_cyls; l.heads = m_heads; l.firstId = sh.firstId;
        if (!m_tracks.isEmpty()) l.mode = m_tracks[0].mode;
    }
    Image fresh;
    if (!fresh.open(path, err)) return false;
    if (as == Kind::Raw && !fresh.setRawLayout(l, err)) return false;
    if (as != Kind::Raw) fresh.m_layout = l;
    *this = fresh;
    return true;
}

// ------------------------------------------------------------------------------------------- TeleDisk

bool Image::parseTd0(const QByteArray &file, QString *err) {
    bool advanced = file[0] == 't';
    int version = quint8(file[4]);
    m_td0Rate = quint8(file[5]) & 0x7F;
    m_td0Fm = (quint8(file[5]) & 0x80) != 0;
    int stepping = quint8(file[7]);
    QByteArray b;
    if (advanced) {
        if (version < 20) { *err = QString("TeleDisk %1.%2 'advanced compression' (before version 2.0) is not supported").arg(version / 10).arg(version % 10); return false; }
        b = lzhufDecode(file.mid(12), 8 * 1024 * 1024);
    } else {
        b = file.mid(12);
    }
    int o = 0;
    if (stepping & 0x80) {
        if (b.size() < 10) { *err = "TeleDisk comment block is truncated"; return false; }
        int len = u16(b, 2);
        if (10 + len > b.size()) { *err = "TeleDisk comment block is truncated"; return false; }
        QByteArray c = b.mid(10, len);
        c.replace('\0', '\n');
        m_comment = QString::fromLatin1(c).trimmed();
        o = 10 + len;
    }
    static const int imdModeFm[3] = {2, 1, 0}, imdModeMfm[3] = {5, 4, 3};
    for (;;) {
        if (o >= b.size()) { *err = "TeleDisk file ends without an end-of-disc marker"; return false; }
        int ns = quint8(b[o]);
        if (ns == 0xFF) break;
        if (o + 4 > b.size()) { *err = "TeleDisk track header is truncated"; return false; }
        Track t;
        t.cyl = quint8(b[o + 1]);
        t.head = quint8(b[o + 2]) & 1;
        bool fm = m_td0Fm || (quint8(b[o + 2]) & 0x80);
        int r = qMin(m_td0Rate, 2);
        t.mode = fm ? imdModeFm[r] : imdModeMfm[r];
        o += 4;
        for (int k = 0; k < ns; ++k) {
            if (o + 6 > b.size()) { *err = "TeleDisk sector header is truncated"; return false; }
            Sector s;
            s.c = quint8(b[o]); s.h = quint8(b[o + 1]); s.r = quint8(b[o + 2]); s.n = quint8(b[o + 3]);
            int flags = quint8(b[o + 4]);
            o += 6;
            s.deleted = flags & 0x04;
            s.crcError = flags & 0x02;
            s.size = s.n <= 6 ? 128 << s.n : 0;
            if ((flags & 0x30) || s.n > 6) {
                s.missing = true;
                if (!(flags & 0x30) && s.n > 6) { // a data block is still present: skip it
                    if (o + 2 > b.size()) { *err = "TeleDisk sector is truncated"; return false; }
                    o += 2 + u16(b, o);
                }
                t.secs.append(s);
                continue;
            }
            if (o + 3 > b.size()) { *err = "TeleDisk sector is truncated"; return false; }
            int dlen = u16(b, o);
            int method = quint8(b[o + 2]);
            int end = o + 2 + dlen;
            if (dlen < 1 || end > b.size()) { *err = "TeleDisk sector data is truncated"; return false; }
            int p = o + 3;
            QByteArray d;
            if (method == 0) {
                d = b.mid(p, s.size);
            } else if (method == 1) {
                while (d.size() < s.size && p + 4 <= end) {
                    int count = u16(b, p);
                    for (int j = 0; j < count && d.size() < s.size; ++j) { d += b[p + 2]; d += b[p + 3]; }
                    p += 4;
                }
            } else if (method == 2) {
                while (d.size() < s.size && p + 2 <= end) {
                    int len = quint8(b[p]), rep = quint8(b[p + 1]);
                    p += 2;
                    if (len == 0) { d += b.mid(p, rep); p += rep; }
                    else {
                        int unit = 1 << len;
                        QByteArray pat = b.mid(p, unit);
                        for (int j = 0; j < rep; ++j) d += pat;
                        p += unit;
                    }
                }
            } else {
                *err = QString("TeleDisk sector encoding %1 is not known").arg(method);
                return false;
            }
            if (d.size() < s.size) d.append(QByteArray(s.size - d.size(), '\0'));
            s.data = d.left(s.size);
            o = end;
            t.secs.append(s);
        }
        m_tracks.append(t);
    }
    return true;
}

} // namespace Disk
