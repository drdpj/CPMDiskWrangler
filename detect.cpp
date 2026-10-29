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
#include "detect.h"
#include "torchfs.h"

#include <QCryptographicHash>
#include <QHash>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <climits>

namespace Detect {

namespace {

// ------------------------------------------------------------------------------------------- reading

bool readLogical(const Disk::Image &img, const Cpm::Format &f, qint64 off, int len, QByteArray *out) {
    out->clear();
    out->reserve(len);
    const int sl = f.seclen;
    qint64 abs = qint64(f.dataStart) * sl + off;
    QByteArray s;
    while (len > 0) {
        int lsec = int(abs / sl), within = int(abs % sl);
        int c, h, i;
        if (!f.locate(lsec, &c, &h, &i) || c >= img.cylinders() || h >= img.heads()) return false;
        if (!img.read(c, h, i, &s) || s.size() != sl) return false;
        int n = qMin(len, sl - within);
        out->append(s.constData() + within, n);
        abs += n;
        len -= n;
    }
    return true;
}

// ------------------------------------------------------------------------------------------- entries

// KInert: a user entry that holds nothing and has a name no CP/M could have made (see Cpm::Volume::inertEntry);
// one is tolerated (a real Acorn Z80 disc has one), more count as nonsense.
enum Kind { KEmpty, KFile, KOther, KLabel, KStamp, KBad, KInert };

struct EInfo {
    Kind k = KEmpty;
    int user = 0, ex = 0, s2 = 0, rc = 0;
    QByteArray name;            // 11 bytes, attribute bits cleared
    const unsigned char *raw = nullptr;
};

bool nameOk(const unsigned char *e) {
    if ((e[1] & 0x7F) == ' ') return false;
    for (int k = 1; k <= 11; ++k) {
        int c = e[k] & 0x7F;
        if (c < 0x20 || c == 0x7F) return false;
        if (strchr("<>,;:=?*[]|", c)) return false;
    }
    return true;
}

EInfo classify(const unsigned char *e, int index) {
    EInfo x;
    x.raw = e;
    int st = e[0];
    if (st == 0xE5) { x.k = KEmpty; return x; }
    if (st == 0x21) { x.k = (index & 3) == 3 ? KStamp : KBad; return x; }
    if (st == 0x20) { x.k = nameOk(e) && (e[12] & 0x0E) == 0 ? KLabel : KBad; return x; }
    if (st > 31) { x.k = KBad; return x; }
    if (!nameOk(e) || e[15] > 0x80 || (e[12] & 0xE0) || (e[14] & 0xC0) || e[13] > 0x80) {
        x.k = KBad;
        if (st <= 15 && e[15] == 0) {
            bool empty = true, control = false, anyName = false;
            for (int k = 16; k < 32; ++k) if (e[k]) empty = false;
            for (int k = 1; k <= 11; ++k) {
                if ((e[k] & 0x7F) < 0x20) control = true;
                if (e[k]) anyName = true;
            }
            if (empty && control && anyName) x.k = KInert;      // (an all-zero entry is not: zeroed sectors stay bad)
        }
        return x;
    }
    x.k = st <= 15 ? KFile : KOther;
    x.user = st;
    x.ex = e[12];
    x.s2 = e[14];
    x.rc = e[15];
    x.name = QByteArray(reinterpret_cast<const char *>(e + 1), 11);
    for (char &c : x.name) c = char(c & 0x7F);
    return x;
}

struct DirScore {
    double score = 0;
    int fileEntries = 0, bad = 0, badPtr = 0, cross = 0, exact = 0, mismatch = 0, labels = 0, stamps = 0, inert = 0;
    int files = 0, lowest = INT_MAX;
};

DirScore scoreDir(const Cpm::Format &f, const QVector<EInfo> &ent) {
    DirScore d;
    QSet<int> used;
    QSet<QByteArray> extKeys, names;
    int dupExt = 0, sparse = 0;
    const int n = qMin(f.maxdir, ent.size());
    for (int i = 0; i < n; ++i) {
        const EInfo &x = ent[i];
        switch (x.k) {
        case KEmpty: break;
        case KBad: d.bad++; break;
        case KInert: if (++d.inert > 1) d.bad++; break;
        case KOther: d.score += 1; break;
        case KLabel: if (++d.labels > 1) d.bad++; break;
        case KStamp: d.stamps++; break;
        case KFile: {
            d.fileEntries++;
            int lext = (x.s2 & 0x3F) * 32 + (x.ex & 0x1F);
            int recs = (x.ex & f.exm) * 128 + qMin(x.rc, 128);
            QByteArray key = QByteArray(1, char(x.user)) + x.name;
            names.insert(key);
            QByteArray ek = key + QByteArray::number(lext);
            if (extKeys.contains(ek)) dupExt++;
            extKeys.insert(ek);
            int nz = 0;
            bool zeroSeen = false;
            for (int k = 0; k < f.ptrsPerEntry; ++k) {
                int b = f.ptr16 ? (x.raw[16 + 2 * k] | x.raw[17 + 2 * k] << 8) : x.raw[16 + k];
                if (!b) { zeroSeen = true; continue; }
                if (zeroSeen) sparse++;
                ++nz;
                if (b > f.dsm || b < f.dirBlocks) { d.badPtr++; continue; }
                if (used.contains(b)) d.cross++;
                used.insert(b);
                d.lowest = qMin(d.lowest, b);
            }
            int need = (recs * 128 + f.blocksize - 1) / f.blocksize;
            if (nz == need) d.exact++; else d.mismatch++;
            break;
        }
        }
    }
    d.files = names.size();
    d.score += 4.0 * d.fileEntries + 2.0 * d.exact - 3.0 * d.mismatch - 15.0 * d.bad - 8.0 * d.badPtr - 8.0 * d.cross -
               8.0 * dupExt - 1.0 * sparse + 1.0 * qMin(d.labels, 1) + 0.5 * d.stamps;
    if (d.fileEntries > 0 && d.lowest == f.dirBlocks) d.score += 6;
    return d;
}

QVector<EInfo> classifyAll(const QByteArray &dir) {
    QVector<EInfo> v;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(dir.constData());
    for (int i = 0; i + 32 <= dir.size(); i += 32) v.append(classify(p + i, i / 32));
    return v;
}

// ------------------------------------------------------------------------------------------- contents

bool isTextByte(unsigned char c) {
    c &= 0x7F;                          // WordStar sets bit 7 on some characters
    return c >= 0x20 || c == '\r' || c == '\n' || c == '\t' || c == 0x0C || c == 0x1A || c == 0x08;
}

double printable(const QByteArray &r) {
    if (r.isEmpty()) return 0;
    int n = 0;
    for (char c : r) if (isTextByte(quint8(c))) ++n;
    return double(n) / r.size();
}

bool textExtension(const QByteArray &n11) {
    static const QSet<QByteArray> exts = {"TXT", "DOC", "ASM", "MAC", "Z80", "PRN", "LST", "SUB", "ME ", "C  ",
                                          "H  ", "PAS", "FOR", "PLI", "INF", "MSG", "DOK", "TEX", "MAN"};
    return exts.contains(n11.mid(8, 3));
}

struct ContentScore { int textGood = 0, textBad = 0, startsBlank = 0, comGood = 0, comBad = 0; };

ContentScore scoreContents(const Disk::Image &img, const Cpm::Format &f, const QVector<EInfo> &ent) {
    ContentScore cs;
    // gather files: key -> (lext, entry index)
    QMap<QByteArray, QVector<QPair<int, int>>> files;
    for (int i = 0; i < qMin(f.maxdir, ent.size()); ++i)
        if (ent[i].k == KFile) {
            const EInfo &x = ent[i];
            files[QByteArray(1, char(x.user)) + x.name].append(qMakePair((x.s2 & 0x3F) * 32 + (x.ex & 0x1F), i));
        }
    int examined = 0;
    for (auto it = files.begin(); it != files.end() && examined < 32; ++it) {
        auto l = it.value();
        std::sort(l.begin(), l.end());
        const EInfo &x0 = ent[l.first().second];
        if ((l.first().first & ~f.exm) != 0) continue;            // starts with a hole: skip
        int recs0 = (x0.ex & f.exm) * 128 + qMin(x0.rc, 128);
        if (recs0 == 0) continue;
        int firstBlock = f.ptr16 ? (x0.raw[16] | x0.raw[17] << 8) : x0.raw[16];
        if (firstBlock == 0 || firstBlock > f.dsm || firstBlock < f.dirBlocks) continue;
        // read up to 8K of the first extent, block by block
        QByteArray data;
        bool ok = true;
        for (int k = 0; k < f.ptrsPerEntry && data.size() < 8192 && data.size() < recs0 * 128; ++k) {
            int b = f.ptr16 ? (x0.raw[16 + 2 * k] | x0.raw[17 + 2 * k] << 8) : x0.raw[16 + k];
            if (b == 0 || b > f.dsm) break;
            QByteArray chunk;
            int want = qMin(f.blocksize, recs0 * 128 - data.size());
            if (!readLogical(img, f, qint64(b) * f.blocksize, want, &chunk)) { ok = false; break; }
            data += chunk;
        }
        if (!ok || data.size() < 128) continue;
        // the file's real end: the last record's byte count (cpmtools, CP/M 3), else trailing NUL / E5 padding
        if (l.size() == 1 && x0.raw[13] > 0 && x0.raw[13] < 128 && data.size() == recs0 * 128)
            data.truncate(data.size() - (128 - x0.raw[13]));
        else if (l.size() == 1 && data.size() == recs0 * 128 && data.indexOf(char(0x1A)) < 0)
            while (!data.isEmpty() && (data.endsWith(char(0)) || data.endsWith(char(0xE5)))) data.chop(1);
        if (data.isEmpty()) continue;
        ++examined;
        QByteArray first = data.left(128);
        if (first.size() == 128 && first.count(char(0xE5)) == 128) { cs.startsBlank++; continue; }
        QByteArray ext = x0.name.mid(8, 3);
        if (ext == "COM") {
            static const QByteArray starts = QByteArray::fromHex("c3312111013e0e2acd3aaff318eddd fd06c5d5e5f5 2232 00");
            if (starts.contains(first[0])) cs.comGood++; else cs.comBad++;
            continue;
        }
        bool textual = textExtension(x0.name) || printable(first) >= 0.97;
        if (!textual || printable(first) < 0.80) {
            static const QSet<QByteArray> sure = {"TXT", "ASM", "MAC", "Z80", "SUB", "PRN", "LST"};
            if (sure.contains(x0.name.mid(8, 3)) && printable(first) < 0.5) cs.textBad++;
            continue;
        }
        // a text file should be text record after record until its end-of-file mark
        bool good = true;
        int eof = data.indexOf(char(0x1A));
        int records = (data.size() + 127) / 128;
        for (int r = 0; r < records; ++r) {
            if (eof >= 0 && r * 128 > eof) break;
            QByteArray rec = data.mid(r * 128, 128);
            if (eof >= 0 && eof < r * 128 + 128) rec = rec.left(eof - r * 128);
            if (rec.size() >= 16 && printable(rec) < 0.85) { good = false; break; }
        }
        if (good) cs.textGood++; else cs.textBad++;
    }
    return cs;
}

QString listingSignature(const Cpm::Format &f, const QVector<EInfo> &ent) {
    QStringList l;
    for (int i = 0; i < qMin(f.maxdir, ent.size()); ++i)
        if (ent[i].k == KFile) {
            const EInfo &x = ent[i];
            l << QString("%1:%2:%3:%4").arg(x.user).arg(QString::fromLatin1(x.name)).arg(x.ex).arg(x.rc);
        }
    l.sort();
    return l.join('|');
}

// The Amstrad PCW / +3 boot sector spec: the first sector of track 0 describes the disc.
bool pcwSpec(const Disk::Image &img, Cpm::Format *out) {
    QByteArray s;
    if (!img.read(0, 0, 0, &s) || s.size() < 16) return false;
    const unsigned char *b = reinterpret_cast<const unsigned char *>(s.constData());
    if (b[0] != 0 && b[0] != 3) return false;
    int sides = b[1] & 3, tracks = b[2], spt = b[3], psh = b[4], res = b[5], bsh = b[6], dirb = b[7];
    if (sides > 2 || (tracks != 40 && tracks != 80) || spt < 8 || spt > 18 || psh > 3 || res > 5 || bsh < 3 || bsh > 6 ||
        dirb < 1 || dirb > 16)
        return false;
    for (int k = 10; k < 15; ++k) if (b[k] != 0) return false;
    Cpm::Format f;
    f.name = "pcw-spec";
    f.desc = "from the Amstrad boot sector spec";
    f.seclen = 128 << psh;
    f.sectrk = spt;
    f.heads = sides ? 2 : 1;
    f.sides = sides == 2 ? Cpm::SideOrder::OutOut : Cpm::SideOrder::Alt;
    f.tracks = tracks * f.heads;
    f.boottrk = res;
    f.blocksize = 128 << bsh;
    f.maxdir = dirb * f.blocksize / 32;
    f.os = Cpm::Os::V3;
    f.origin = "detected";
    QString e;
    if (!f.derive(&e)) return false;
    *out = f;
    return true;
}

bool physicallyFits(const Cpm::Format &f, const Disk::Image &img, const Disk::Shape &sh) {
    if (img.kind() == Disk::Kind::Raw) {
        const Disk::RawLayout &l = img.rawLayout();
        if (f.t0sectrk > 0 && l.t0sectrk > 0 && (f.t0sectrk != l.t0sectrk || f.t0seclen != l.t0seclen)) return false;
        if (f.separateSides) return f.seclen == l.seclen && f.sectrk == l.sectrk && l.heads == 2 && f.cylinders <= l.cyls;
        if (l.sideBySide && l.heads == 2) return false;          // side after side: only for discs with a filesystem per side
        return f.seclen == l.seclen && f.sectrk == l.sectrk && f.physHeads == l.heads && f.cylinders <= l.cyls &&
               qint64(f.tracks) * f.sectrk + f.offsetSectors <= qint64(l.cyls) * l.heads * l.sectrk;
    }
    if (f.seclen != sh.seclen || f.sectrk != sh.sectrk) return false;
    if (f.physHeads != sh.heads) return false;
    if (f.offsetSectors) return false;
    return f.cylinders <= sh.cyls + 1 && f.cylinders >= sh.cyls - 4;
}

// ------------------------------------------------------------------------------------------- not CP/M at all

// An empty CP/M directory says nothing about the format, so every layout of the right shape reads one on a blank
// disc - and on any disc whose directory area happens to be E5 (an Acorn DFS disc formatted by Acorn's FORM, for
// one). Two checks stop such a disc being read as an empty CP/M disc: the data area must not hold data that no
// directory entry accounts for, and the disc must not carry another filesystem's own structures.

// The disc's first n bytes: a raw file's start, or cylinder 0 head 0's sectors in ID order.
QByteArray discStart(const Disk::Image &img, int n, int *seclen) {
    QByteArray out, s;
    *seclen = 0;
    if (img.kind() == Disk::Kind::Raw) return img.rawBytes().left(int(qMin<qint64>(n, img.rawFileSize())));
    const Disk::Track *t = img.track(0, 0);
    if (!t || t->secs.isEmpty()) return out;
    *seclen = t->secs[t->byId.value(0)].size;
    for (int i = 0; i < t->byId.size() && out.size() < n; ++i) {
        if (!img.read(0, 0, i, &s)) break;
        out += s;
    }
    return out.left(n);
}

// An Acorn DFS catalogue in the first two 256-byte sectors (the same tests as Acorn Disk Wrangler's, a little
// stricter: unused bits clear, a printable or NUL title, and a standard disc size when there are no files).
bool dfsCatalogue(const QByteArray &b) {
    if (b.size() < 512) return false;
    const unsigned char *s0 = reinterpret_cast<const unsigned char *>(b.constData()), *s1 = s0 + 256;
    int cnt = s1[5];
    if (cnt % 8 || cnt > 31 * 8 || (s1[6] & 0xCC)) return false;
    int sec = (s1[6] & 3) << 8 | s1[7];
    if (sec < 2 || sec > 800 || (cnt == 0 && sec != 400 && sec != 800)) return false;
    for (int k = 0; k < 12; ++k) {
        int c = (k < 8 ? s0[k] : s1[k - 8]) & 0x7F;
        if (c && (c < 0x20 || c > 0x7E)) return false;
    }
    for (int i = 1; i <= cnt / 8; ++i) {
        for (int k = 0; k < 8; ++k) {
            int c = s0[i * 8 + k] & 0x7F;
            if (c < 0x20 || c > 0x7E) return false;
        }
        int hi = s1[i * 8 + 6];
        int start = (hi & 3) << 8 | s1[i * 8 + 7];
        int len = s1[i * 8 + 4] | s1[i * 8 + 5] << 8 | ((hi >> 4) & 3) << 16;
        if (start < 2 || start + (len + 255) / 256 > sec) return false;
    }
    return true;
}

// Another filesystem the disc plainly carries, in words, or empty.
QString foreignFilesystem(const Disk::Image &img) {
    int seclen = 0;
    QByteArray b = discStart(img, 4096, &seclen);
    auto at = [&](int off, const char *magic) { return b.size() >= off + 4 && b.mid(off, 4) == magic; };
    auto hugo = [&](int off) { return at(off, "Hugo") || at(off, "Nick"); };
    const qint64 size = img.kind() == Disk::Kind::Raw ? img.rawFileSize() : 0;
    // ADFS old map: the root directory (sector 2, five sectors) starts and ends with Hugo / Nick
    if ((seclen == 0 || seclen == 256) && hugo(0x201) && hugo(0x6FB))
        return "an Acorn ADFS disc (its root directory is where ADFS keeps it)";
    // ADFS new map (D/E floppies): recognised by the root directory's name
    if ((seclen == 0 || seclen == 1024) && (size == 0 || size == 819200 || size == 1638400) && (hugo(0x401) || hugo(0x801)))
        return "an Acorn ADFS D/E disc";
    // PC FAT: a boot record with a BPB, or (DOS 1.x, Acorn DOS Plus 800K) a FAT starting with its media byte
    auto u8 = [&](int i) { return i < b.size() ? quint8(b[i]) : 0; };
    if (b.size() >= 512 && (u8(11) | u8(12) << 8) == 512 && u8(21) >= 0xF0 && u8(13) && !(u8(13) & (u8(13) - 1)))
        return "an MS-DOS (FAT) disc (its first sector holds a FAT boot record)";
    auto fatSig = [&](int off) { int m = u8(off); return (m == 0xF0 || m >= 0xF8) && u8(off + 1) == 0xFF && u8(off + 2) == 0xFF; };
    if (b.size() >= 515 && (fatSig(0) || fatSig(512)))
        return "an MS-DOS (FAT) disc (it starts with a FAT)";
    // Acorn DFS: a catalogue in the first two sectors - unless it is the one Acorn's CP/M writes ($.CPMDISC)
    if ((seclen == 0 || seclen == 256) && dfsCatalogue(b) && !Cpm::isAcornDfsCatalogue(b))
        return "an Acorn DFS disc (a DFS catalogue fills its first two sectors)";
    return QString();
}

// True when the directory area is a single repeated byte (a freshly formatted directory, not one of deleted files).
bool directoryBlank(const Disk::Image &img, const Cpm::Format &f) {
    QByteArray d;
    if (!readLogical(img, f, 0, f.maxdir * 32, &d) || d.isEmpty()) return false;
    return d.count(d[0]) == d.size();
}

// 128-byte records anywhere on the disc, outside the format's boot tracks, that hold anything but one repeated
// byte. Formatting writes every track, so a freshly formatted disc has none; a disc with an empty directory and
// data elsewhere is another kind of disc. (Data before a format's offset counts too: it is another partition,
// and reading an empty one of a disc that holds data says nothing.)
int strayRecords(const Disk::Image &img, const Cpm::Format &f) {
    QSet<quint32> boot;
    auto key = [](int c, int h, int i) { return quint32(c) << 16 | quint32(h) << 8 | quint32(i); };
    for (int side = 0; side < (f.separateSides ? 2 : 1); ++side) {
        Cpm::Format g = f;
        if (f.separateSides) {
            g.fixedHead = side;
            g.derived = false;
            QString e;
            if (!g.derive(&e)) continue;
        }
        int c, h, i;
        for (int l = g.offsetSectors; l < g.dataStart; ++l)
            if (g.locate(l, &c, &h, &i)) boot.insert(key(c, h, i));
    }
    int n = 0;
    QByteArray s;
    for (int c = 0; c < img.cylinders(); ++c)
        for (int h = 0; h < img.heads(); ++h)
            for (int i = 0; i < img.sectorCount(c, h); ++i) {
                if (boot.contains(key(c, h, i)) || !img.read(c, h, i, &s)) continue;
                for (int r = 0; r + 128 <= s.size(); r += 128)
                    if (s.mid(r, 128).count(s[r]) != 128) ++n;
            }
    return n;
}

Candidate evaluateSide(const Disk::Image &img, const Cpm::Format &f, int head);

// An empty reading (a blank directory, and no files on the other side of a disc with a filesystem per side) of a
// disc that holds data. A few records are tolerated (a boot loader spilling past its tracks, say).
bool emptyButUsed(const Disk::Image &img, const Cpm::Format &f) {
    if (!directoryBlank(img, f)) return false;
    if (f.separateSides && evaluateSide(img, f, 1 - f.fixedHead).files > 0) return false;
    return strayRecords(img, f) >= 4;
}

const Cpm::Format *libraryMatch(const Cpm::Format &f, int firstId, bool haveIds) {
    const Cpm::Format *best = nullptr;
    for (const Cpm::Format &l : Cpm::library())
        if (l.sameLayout(f)) {
            if (haveIds && l.secbase >= 0 && l.secbase != firstId) continue;
            if (!best || (haveIds && l.secbase == firstId && best->secbase != firstId) ||
                (best->desc.isEmpty() && !l.desc.isEmpty() && (!haveIds || best->secbase != firstId)))
                best = &l;
        }
    return best;
}

// Torch CP/N (format key "torchcpn"): its own directory, not a CP/M one. A reading needs the map's fixed bits;
// then +4 a file, +2 a file whose index chain is sound, -15 an entry that isn't a file, -8 a broken chain or a sector
// used twice, +10 for the map and +6 for the formatter's two sectors.
Candidate evaluateTorch(const Disk::Image &img, Candidate c) {
    Torch::Scan s = Torch::scan([&](int n, QByteArray *out) { return readLogical(img, c.fmt, qint64(n) * Torch::SecLen, Torch::SecLen, out); });
    if (!s.readable) { c.score = -1e9; c.evidence << "the Torch CP/N directory tracks can't be read with this layout"; return c; }
    if (!s.mapOk) { c.score = -1e9; c.evidence << "no Torch CP/N allocation map here (its fixed bits are missing)"; return c; }
    c.files = s.files;
    c.fileEntries = s.files;
    c.badEntries = s.bad;
    c.badPointers = s.badPtr;
    c.crossLinks = s.cross;
    c.exactCounts = s.exact;
    c.score = 10 + 4.0 * s.files + 2.0 * s.exact - 15.0 * s.bad - 8.0 * (s.badPtr + s.cross);
    c.evidence << QString("%1 files in %2 directory entries (Torch CP/N)").arg(s.files).arg(s.files + s.bad);
    c.evidence << "a Torch CP/N allocation map (its fixed bits set, the directory tracks in use)";
    if (s.signature) { c.score += 6; c.evidence << "track 1 sectors 8-9 hold the bytes Torch's formatter writes"; }
    if (s.bad) c.evidence << QString("%1 directory entries make no sense").arg(s.bad);
    if (s.badPtr) c.evidence << QString("%1 files whose index sectors don't hold together").arg(s.badPtr);
    if (s.cross) c.evidence << QString("%1 sectors used twice").arg(s.cross);
    if (s.files) c.evidence << QString("index chains match the record counts in %1 of %2 files").arg(s.exact).arg(s.files);
    c.evidence.prepend("signature:" + QString(QCryptographicHash::hash(("torch|" + s.names.join('|')).toUtf8(), QCryptographicHash::Md5).toHex()));
    c.base = c.score;
    c.baseLines = c.evidence.size();
    return c;
}

} // namespace

QString Candidate::key() const {
    const Cpm::Format &f = fmt;
    QString sk;
    for (int v : f.xlat) sk += QString::number(v) + ",";
    return QString("%1x%2x%3x%4x%5|").arg(raw.seclen).arg(raw.sectrk).arg(raw.heads).arg(raw.t0sectrk).arg(raw.sideBySide) +
           QString("%1|%2|%3|%4|%5|").arg(f.seclen).arg(f.sectrk).arg(f.tracks).arg(f.separateSides ? 9 : f.physHeads == 1 ? 0 : int(f.sides)).arg(f.dataStart) +
           QString("%1|%2|%3|%4%5").arg(f.blocksize).arg(f.maxdir).arg(f.dirBlocks).arg(sk).arg(f.torch ? "|torch" : "");
}

Candidate evaluate(const Disk::Image &img, const Cpm::Format &fIn, bool deep) {
    Candidate c;
    c.fmt = fIn;
    QString e;
    if (!c.fmt.derived && !c.fmt.derive(&e)) { c.score = -1e9; c.evidence << "invalid: " + e; return c; }
    if (img.kind() == Disk::Kind::Raw) c.raw = img.rawLayout();
    if (c.fmt.torch) return evaluateTorch(img, c);
    QByteArray dir;
    if (!readLogical(img, c.fmt, 0, c.fmt.maxdir * 32, &dir)) {
        c.score = -1e9;
        c.evidence << "the directory area can't be read with this layout";
        return c;
    }
    QVector<EInfo> ent = classifyAll(dir);
    DirScore d = scoreDir(c.fmt, ent);
    c.score = d.score;
    c.files = d.files;
    c.fileEntries = d.fileEntries;
    c.badEntries = d.bad;
    c.badPointers = d.badPtr;
    c.crossLinks = d.cross;
    c.exactCounts = d.exact;
    c.evidence << QString("%1 files in %2 directory entries").arg(d.files).arg(d.fileEntries);
    if (d.bad) c.evidence << QString("%1 directory entries make no sense").arg(d.bad);
    if (d.badPtr) c.evidence << QString("%1 block pointers out of range").arg(d.badPtr);
    if (d.cross) c.evidence << QString("%1 blocks claimed twice").arg(d.cross);
    if (d.fileEntries) c.evidence << QString("block counts match record counts in %1 of %2 entries").arg(d.exact).arg(d.fileEntries);
    if (d.fileEntries && d.lowest == c.fmt.dirBlocks) c.evidence << "the first file block follows the directory";
    if (d.inert) c.evidence << QString("%1 directory entr%2 holding nothing, with a name no CP/M would make (not a file)")
                                   .arg(d.inert).arg(d.inert == 1 ? "y" : "ies");
    if (d.labels) c.evidence << "has a CP/M 3 disc label";
    if (d.stamps) {
        c.evidence << "has CP/M 3 / P2DOS time stamp entries";
        if (c.fmt.os == Cpm::Os::V22) c.fmt.os = Cpm::Os::V3;
    }
    if (deep && d.fileEntries) {
        ContentScore cs = scoreContents(img, c.fmt, ent);
        c.textGood = cs.textGood;
        c.textBad = cs.textBad;
        c.startsBlank = cs.startsBlank;
        c.score += 3.0 * cs.textGood - 8.0 * cs.textBad - 4.0 * cs.startsBlank + 1.0 * cs.comGood - 1.0 * cs.comBad;
        if (cs.textGood || cs.textBad) c.evidence << QString("text files read as text: %1 of %2").arg(cs.textGood).arg(cs.textGood + cs.textBad);
        if (cs.comGood || cs.comBad) c.evidence << QString(".COM files starting with a likely instruction: %1 of %2").arg(cs.comGood).arg(cs.comGood + cs.comBad);
        if (cs.startsBlank) c.evidence << QString("%1 files start in never-written (E5) space").arg(cs.startsBlank);
    }
    c.evidence.prepend("signature:" + QString(QCryptographicHash::hash(listingSignature(c.fmt, ent).toUtf8(), QCryptographicHash::Md5).toHex()));
    c.base = c.score;
    c.baseLines = c.evidence.size();
    return c;
}

Candidate reevaluate(const Disk::Image &img, const Candidate &old) {
    Candidate fresh = evaluate(img, old.fmt, true);
    if (fresh.score < -1e8) return old;
    Candidate c = old;
    c.score = old.score - old.base + fresh.score;
    c.base = fresh.score;
    c.files = fresh.files; c.fileEntries = fresh.fileEntries; c.badEntries = fresh.badEntries;
    c.badPointers = fresh.badPointers; c.crossLinks = fresh.crossLinks; c.exactCounts = fresh.exactCounts;
    c.textGood = fresh.textGood; c.textBad = fresh.textBad; c.startsBlank = fresh.startsBlank;
    c.evidence = fresh.evidence + old.evidence.mid(old.baseLines);
    c.baseLines = fresh.baseLines;
    return c;
}

namespace {
Candidate evaluateSide(const Disk::Image &img, const Cpm::Format &f, int head) {
    Cpm::Format g = f;
    g.fixedHead = head;
    g.derived = false;
    return evaluate(img, g, false);
}
} // namespace

Result detect(Disk::Image &img, const QString &remembered, int keep) {
    Result res;
    QVector<Candidate> all;
    QVector<Disk::RawLayout> layouts;
    const bool raw = img.kind() == Disk::Kind::Raw;

    // ----- physical layouts to try
    if (raw) {
        qint64 size = img.rawFileSize();
        auto addLayout = [&](const Disk::RawLayout &l) {
            for (const Disk::RawLayout &o : layouts)
                if (o.sameShape(l)) return;
            layouts.append(l);
        };
        for (const Cpm::Format &f : Cpm::library()) {
            if (f.t0sectrk > 0 && !f.offsetSectors) {
                // a track 0 of its own size: one side, or two sides (side after side, or cylinder by cylinder),
                // each side with its own track 0 - a disc with a filesystem per side, or one over both sides
                Disk::RawLayout l;
                l.seclen = f.seclen; l.sectrk = f.sectrk; l.t0sectrk = f.t0sectrk; l.t0seclen = f.t0seclen;
                l.heads = 1;
                l.cyls = f.physHeads == 1 ? f.tracks : f.cylinders;
                if (f.physHeads == 1 && size == l.bytes()) addLayout(l);
                l.heads = 2;
                if (size == l.bytes()) {
                    l.sideBySide = true;
                    addLayout(l);
                    l.sideBySide = false;
                    addLayout(l);
                }
                continue;
            }
            qint64 full = (qint64(f.tracks) * f.sectrk + f.offsetSectors) * f.seclen;
            qint64 trackBytes = qint64(f.sectrk) * f.seclen;
            bool fits = size == full || (size < full && size % trackBytes == 0 && size > qint64(f.dataStart + 4) * f.seclen &&
                                         size * 10 >= full * 6);
            if (!fits) continue;
            Disk::RawLayout l;
            l.seclen = f.seclen; l.sectrk = f.sectrk; l.heads = f.physHeads;
            l.cyls = int((qint64(f.tracks) * f.sectrk + f.offsetSectors + qint64(f.sectrk) * f.physHeads - 1) / (qint64(f.sectrk) * f.physHeads));
            addLayout(l);
        }
        static const int seclens[] = {128, 256, 512, 1024};
        static const int spts[] = {5, 8, 9, 10, 11, 15, 16, 17, 18, 26, 32};
        for (int sl : seclens)
            for (int st : spts)
                for (int h = 1; h <= 2; ++h) {
                    qint64 tb = qint64(sl) * st * h;
                    if (size % tb) continue;
                    qint64 cyl = size / tb;
                    if (cyl < 30 || cyl > 86 || (h == 1 && cyl > 86)) continue;
                    if (h == 2 && cyl < 35) continue;
                    Disk::RawLayout l;
                    l.seclen = sl; l.sectrk = st; l.heads = h; l.cyls = int(cyl);
                    addLayout(l);
                }
        if (layouts.isEmpty()) {
            res.physical = QString("raw image of %1 bytes, a size no known format or common geometry has").arg(size);
            res.confidence = "none";
            res.why = "Choose a format by hand (Tools > Disc format).";
            return res;
        }
    } else {
        layouts.append(Disk::RawLayout());          // the container's own
    }

    Cpm::Format rememberedFmt;
    bool haveRemembered = false;
    if (!remembered.isEmpty()) {
        QVector<Cpm::Format> r = Cpm::parseDiskdefs(remembered, "remembered", nullptr);
        if (!r.isEmpty()) { rememberedFmt = r.first(); haveRemembered = true; }
    }

    for (const Disk::RawLayout &layout : layouts) {
        if (raw) {
            QString e;
            Disk::RawLayout l = layout;
            l.firstId = 1;
            if (!img.setRawLayout(l, &e)) continue;
        }
        Disk::Shape sh = img.shape();
        const bool haveIds = !raw;
        int heads = raw ? layout.heads : sh.heads;
        int cyls = raw ? layout.cyls : sh.cyls;
        int seclen = raw ? layout.seclen : sh.seclen;
        int sectrk = raw ? layout.sectrk : sh.sectrk;
        if (seclen <= 0 || sectrk <= 0) continue;

        // ----- known formats (a definition that doesn't say how many sides is tried both ways)
        for (const Cpm::Format &lf : Cpm::library()) {
            Cpm::Format f = lf;
            // a single-sided format on a two-sided disc: perhaps a separate disc on each side
            if (lf.physHeads == 1 && (raw ? layout.heads : sh.heads) == 2) {
                Cpm::Format s = lf;
                s.separateSides = true;
                s.heads = 2;
                QString e;
                if (s.derive(&e) && physicallyFits(s, img, sh)) {
                    Candidate c = evaluate(img, s, true);
                    Cpm::Format s1 = c.fmt;
                    s1.fixedHead = 1;
                    s1.derived = false;
                    Candidate c1 = evaluate(img, s1, true);
                    if (c.score > -1e8) {
                        c.fromLibrary = true;
                        c.score += 3;
                        c.evidence << "a known format (" + lf.origin + "), a separate disc on each side";
                        if (c1.score > -5) {
                            c.score += 2 + qMax(0.0, c1.score) * 0.5;
                            c.evidence << QString("side 1: %1 files, %2").arg(c1.files).arg(c1.badEntries ? "with nonsense entries" : "a sensible directory");
                        } else {
                            c.score -= 4;
                            c.evidence << "side 1 doesn't read as a disc of this format";
                        }
                        if (lf.t0sectrk > 0) {
                            const Disk::Track *t0 = img.track(0, 0), *t1 = img.track(0, 1);
                            bool same = t0 && t1 && t0->secs.size() == lf.t0sectrk && t1->secs.size() == lf.t0sectrk &&
                                        t0->secs[0].size == lf.t0seclen && (t0->mode < 0 || (t0->mode <= 2) == lf.t0fm);
                            if (same) { c.score += 3; c.evidence << QString("track 0 of each side is %1 x %2, as this format's is").arg(lf.t0sectrk).arg(lf.t0seclen); }
                        }
                        all.append(c);
                    }
                }
            }
            if (!physicallyFits(f, img, sh) && lf.heads == 0 && lf.tracks % 2 == 0) {
                f.heads = lf.physHeads == 1 ? 2 : 1;
                QString e;
                if (!f.derive(&e) || !physicallyFits(f, img, sh)) continue;
            } else if (!physicallyFits(f, img, sh)) {
                continue;
            }
            Candidate c = evaluate(img, f, true);
            if (c.score < -1e8) continue;
            c.fromLibrary = true;
            c.score += 3;
            c.evidence << "a known format (" + f.origin + ")";
            if (f.t0sectrk > 0) {
                const Disk::Track *t0 = img.track(0, 0);
                bool same = t0 && t0->secs.size() == f.t0sectrk && !t0->secs.isEmpty() && t0->secs[0].size == f.t0seclen &&
                            (t0->mode < 0 || (t0->mode <= 2) == f.t0fm);
                if (same) { c.score += 3; c.evidence << QString("track 0 is %1 x %2 %3, as this format's is").arg(f.t0sectrk).arg(f.t0seclen).arg(f.t0fm ? "FM" : "MFM"); }
                else { c.score -= 3; c.evidence << "track 0 is not the shape this format gives it"; }
            }
            if (f.acornDfs) {
                int sl = 0;
                if (Cpm::isAcornDfsCatalogue(discStart(img, 512, &sl))) {
                    c.score += 6;
                    c.evidence << "track 0 holds the DFS catalogue Acorn's CP/M writes ($.CPMDISC)";
                }
            }
            if (haveIds && f.secbase >= 0) {
                if (f.secbase == sh.firstId) { c.score += 4; c.evidence << QString("sector IDs start at %1, as this format's do").arg(f.secbase); }
                else { c.score -= 6; c.evidence << QString("sector IDs start at %1, this format's at %2").arg(sh.firstId).arg(f.secbase); }
            }
            all.append(c);
        }
        // ----- the Amstrad boot-sector spec
        Cpm::Format spec;
        if (pcwSpec(img, &spec) && physicallyFits(spec, img, sh)) {
            Candidate c = evaluate(img, spec, true);
            if (c.score > -1e8) { c.score += 15; c.evidence << "described by the Amstrad boot sector spec"; all.append(c); }
        }
        // ----- remembered
        if (haveRemembered && physicallyFits(rememberedFmt, img, sh)) {
            Candidate c = evaluate(img, rememberedFmt, true);
            if (c.score > -1e8) { c.remembered = true; c.score += 20; c.evidence << "the format chosen for this image before"; all.append(c); }
        }

        // ----- the search: reserved tracks x skew x side order, then block size x directory size
        QVector<Candidate> generic;
        int totalTracks = cyls * heads;
        int maxBoot = qMin(6, totalTracks / 4);
        QVector<Cpm::SideOrder> orders = {Cpm::SideOrder::Alt};
        if (heads == 2) orders << Cpm::SideOrder::OutBack << Cpm::SideOrder::OutOut;
        QVector<int> skews = {1};
        if (sectrk <= 32)                         // hard disc tracks (and their images) are never skewed
            for (int s = 2; s < sectrk; ++s) skews << s;
        static const int blss[] = {1024, 2048, 4096, 8192, 16384};
        for (Cpm::SideOrder so : orders)
            for (int boot = 0; boot <= maxBoot; ++boot)
                for (int skew : skews) {
                    Cpm::Format base;
                    base.name = "detected";
                    base.origin = "detected";
                    base.seclen = seclen; base.sectrk = sectrk; base.tracks = totalTracks; base.heads = heads;
                    base.sides = so; base.boottrk = boot; base.skew = skew;
                    base.blocksize = 16384; base.maxdir = 32;    // only locates the directory
                    QString e;
                    if (!base.derive(&e)) continue;
                    qint64 dataBytes = qint64(totalTracks - boot) * sectrk * seclen;
                    int window = int(qMin<qint64>(2048 * 32, dataBytes / 4));
                    QByteArray dir;
                    if (!readLogical(img, base, 0, window, &dir)) continue;
                    QVector<EInfo> ent = classifyAll(dir);
                    // nothing but blank entries and junk at the start: not a directory under this locator
                    int firstBad = -1, filesSeen = 0;
                    for (int i = 0; i < qMin(ent.size(), 16); ++i) {
                        if (ent[i].k == KBad && firstBad < 0) firstBad = i;
                        if (ent[i].k == KFile) filesSeen++;
                    }
                    if (firstBad == 0 && filesSeen == 0) continue;
                    // directory sizes that fill whole blocks (DRM+1 = blocks * BLS / 32), as real formats do
                    for (int bls : blss)
                        for (int nb = 1; nb <= 16; nb = nb < 4 ? nb + 1 : nb * 2) {
                            int md = nb * bls / 32;
                            if (md * 32 > window || md > 2048) break;
                            Cpm::Format f = base;
                            f.blocksize = bls;
                            f.maxdir = md;
                            if (!f.derive(&e)) continue;
                            DirScore d = scoreDir(f, ent);
                            if (d.fileEntries == 0 && d.labels == 0) {
                                // an empty directory: keep only the plain readings (no skew); everything fits
                                if (skew != 1 || d.bad) continue;
                                d.score = -2;
                            }
                            Candidate c;
                            c.fmt = f;
                            c.score = d.score;
                            if (raw) c.raw = img.rawLayout();
                            generic.append(c);
                        }
                }
        std::sort(generic.begin(), generic.end(), [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
        int deepCount = 0;
        for (const Candidate &g : generic) {
            if (deepCount >= 16) break;
            ++deepCount;
            Candidate c = evaluate(img, g.fmt, true);
            if (const Cpm::Format *lm = libraryMatch(c.fmt, sh.firstId, haveIds)) {
                Cpm::Format named = *lm;
                QString e;
                named.derive(&e);
                c.fmt = named;
                if (c.evidence.join(' ').contains("time stamp") && c.fmt.os == Cpm::Os::V22) c.fmt.os = Cpm::Os::V3;
                c.fromLibrary = true;
                c.score += 3;
                c.evidence << "found by searching; the same layout as " + lm->name;
            } else {
                c.evidence << "found by searching (not a known format)";
            }
            all.append(c);
        }
    }

    // ----- rank, one per layout
    int emptyButUsedCount = 0;
    const Disk::RawLayout lastLayout = raw ? img.rawLayout() : Disk::RawLayout();
    for (Candidate &c : all) c.raw = raw && !c.raw.valid() ? lastLayout : c.raw;
    // an empty reading of a disc that holds data: not this format (see emptyButUsed)
    auto usedButEmpty = [&](const Candidate &c) {
        if (c.files != 0 || c.remembered || c.score < -1e8 || c.fmt.torch) return false;   // Torch: its map says
        if (raw && !img.rawLayout().sameShape(c.raw)) {
            QString e;
            Disk::RawLayout l = c.raw;
            l.firstId = 1;
            if (!img.setRawLayout(l, &e)) return false;
        }
        return emptyButUsed(img, c.fmt);
    };
    Disk::RawLayout droppedLayout;       // the best dropped reading's, to describe the disc if nothing is left
    double droppedScore = -1e9;
    for (Candidate &c : all) {
        if (usedButEmpty(c)) {
            if (c.score > droppedScore) { droppedScore = c.score; droppedLayout = c.raw; }
            c.score = -1e9;          // dropped below
            ++emptyButUsedCount;
            continue;
        }
        if (raw && c.raw.bytes() > img.rawFileSize()) {
            c.score -= 3;
            c.evidence << QString("needs the image to be %1 bytes; it is %2 (padded)").arg(c.raw.bytes()).arg(img.rawFileSize());
        }
        if (c.fromLibrary) {
            QStringList same;
            for (const Cpm::Format &l : Cpm::library())
                if (l.name != c.fmt.name && l.sameLayout(c.fmt)) same << l.name;
            if (!same.isEmpty()) c.evidence << "the same layout is also defined as " + same.join(", ");
        }
    }
    if (raw && !img.rawLayout().sameShape(lastLayout)) {
        QString e;
        img.setRawLayout(lastLayout, &e);
    }
    std::stable_sort(all.begin(), all.end(), [](const Candidate &a, const Candidate &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.fromLibrary != b.fromLibrary) return a.fromLibrary;
        if (a.fmt.desc.isEmpty() != b.fmt.desc.isEmpty()) return !a.fmt.desc.isEmpty();
        return a.fmt.maxdir < b.fmt.maxdir;
    });
    QSet<QString> seen;
    for (const Candidate &c : all) {
        // a reading whose "directory" is mostly nonsense is no reading at all
        if (c.badEntries > 0 && c.badEntries * 2 >= c.fileEntries) continue;
        if (c.score < -5) continue;
        QString k = c.key();
        if (seen.contains(k)) continue;
        seen.insert(k);
        res.ranked.append(c);
        if (res.ranked.size() >= keep) break;
    }
    // physical description
    if (raw && (!res.ranked.isEmpty() || droppedLayout.valid())) {
        QString e;
        img.setRawLayout(res.ranked.isEmpty() ? droppedLayout : res.ranked.first().raw, &e);
    }
    Disk::Shape sh = img.shape();
    res.physical = QString("%1: %2 cylinder%3, %4 side%5, %6 x %7-byte sectors per track")
                       .arg(Disk::kindName(img.kind())).arg(sh.cyls).arg(sh.cyls == 1 ? "" : "s").arg(sh.heads)
                       .arg(sh.heads == 1 ? "" : "s").arg(sh.sectrk).arg(sh.seclen);
    if (!raw) res.physical += QString(", sector IDs from %1").arg(sh.firstId);
    if (sh.oddTracks) res.physical += QString(", %1 track%2 of another shape").arg(sh.oddTracks).arg(sh.oddTracks == 1 ? "" : "s");
    if (sh.allHeads > sh.heads) res.physical += QString(" (side %1 has only odd tracks)").arg(sh.allHeads - 1);
    if (sh.badSectors) res.physical += QString(", %1 bad or missing sector%2").arg(sh.badSectors).arg(sh.badSectors == 1 ? "" : "s");
    if (raw) res.physical += QString(" (raw: %1 bytes)").arg(img.rawFileSize());

    // (a Torch CP/N reading has its own structures to go by; its directory could pass for something else's)
    const QString foreign = !res.ranked.isEmpty() && res.ranked.first().fmt.torch ? QString() : foreignFilesystem(img);
    const QString foreignWhy = "This looks like " + foreign + ", not a CP/M disc. (Acorn Disk Wrangler can open it.) "
                               "If it really is CP/M, choose the format by hand (Tools > Disc format).";
    if (res.ranked.isEmpty()) {
        res.confidence = "none";
        if (!foreign.isEmpty())
            res.why = foreignWhy;
        else if (emptyButUsedCount)
            res.why = "No CP/M files were found: the layouts that fit read an empty directory, but the disc holds data "
                      "no directory entry accounts for, so it is probably not a CP/M disc (or its directory has been "
                      "wiped). Choose a format by hand (Tools > Disc format) if it is.";
        else
            res.why = "No layout gives a readable CP/M directory. Choose a format by hand (Tools > Disc format).";
        return res;
    }
    const Candidate &best = res.ranked.first();
    // Another filesystem's structures and no CP/M files: not a CP/M disc. With CP/M files as well (unlikely, but a
    // boot sector can resemble a FAT), it is shown but never written until the user confirms the format.
    if (!foreign.isEmpty() && best.files == 0 && !best.remembered) {
        res.ranked.clear();
        res.confidence = "none";
        res.why = foreignWhy;
        return res;
    }
    QString bestSig = best.evidence.first();
    bool clean = best.badEntries == 0 && best.badPointers == 0 && best.crossLinks == 0 && best.textBad * 4 <= best.textGood;
    const Candidate *rival = nullptr, *sameReading = nullptr;
    for (int i = 1; i < res.ranked.size(); ++i) {
        const Candidate &o = res.ranked[i];
        if (o.score < best.score - 6) break;
        if (o.evidence.first() != bestSig) { if (!rival) rival = &o; }
        else if (!sameReading && o.fromLibrary) sameReading = &o;
    }
    if (best.files == 0 && !best.fromLibrary && !best.remembered) {
        res.ranked.clear();
        res.confidence = "none";
        res.why = "No CP/M files were found with any layout, and no known CP/M format has this shape: it may not be a "
                  "CP/M disc. Choose a format by hand (Tools > Disc format) if it is.";
        return res;
    }
    if (best.files == 0) {
        res.confidence = "low";
        res.why = "The directory is empty, so the contents can't tell formats apart: " +
                  QString(best.fromLibrary ? "the known format " + best.fmt.name + " was chosen from the disc's shape."
                                           : "no known format has this shape.");
        res.safeToWrite = best.remembered || (best.fromLibrary && !rival && !sameReading && sh.badSectors == 0);
        if (!res.safeToWrite) res.why += " Confirm the format (Tools > Disc format) before changing the disc.";
        return res;
    }
    if (rival) {
        res.confidence = "low";
        res.why = QString("Another layout (%1) reads a different directory and scores nearly as well.").arg(rival->fmt.name);
    } else if (sameReading && !best.remembered) {
        res.confidence = "medium";
        res.why = QString("Other layouts (e.g. %1) list the same files; they differ in how new files would be written.").arg(sameReading->fmt.summary());
    } else {
        res.confidence = "high";
        res.why = "No other layout comes close.";
    }
    if (!clean) {
        if (res.confidence == "high") res.confidence = "medium";
        res.why += " The best reading still has inconsistencies (see Tools > Check disc).";
    }
    // Writing needs a known (or confirmed) format that nothing else with a different layout competes with: a layout
    // that lists the same files but would place new files differently (another directory size, say) is a risk too,
    // unless it is only a search result and the known format is the sensible reading.
    res.safeToWrite = clean && sh.badSectors == 0 && !rival &&
                      (best.remembered || (best.fromLibrary && !sameReading));
    if (!foreign.isEmpty() && !best.remembered) {
        res.safeToWrite = false;
        if (res.confidence == "high") res.confidence = "medium";
        res.why += " The disc also looks like " + foreign + ".";
    }
    if (!res.safeToWrite) res.why += " Changes are off until the format is confirmed (Tools > Disc format).";
    return res;
}

} // namespace Detect
