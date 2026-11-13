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
#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include "../cpmformat.h"
#include "../cpmfs.h"
#include "../cpmvolume.h"
#include "../detect.h"
#include "../hostpolicy.h"
#include "../natsets.h"

using namespace dw::cli;

namespace CpmCli {

// ------------------------------------------------------------------------------------------------ the opened image

static QString tempBeside(const QString &file) {
    const QFileInfo fi(file);
    return fi.absoluteDir().filePath(QString(".dwconv-%1-%2").arg(QUuid::createUuid().toString(QUuid::Id128).left(8), fi.fileName()));
}

// Why a disc whose format isn't confirmed opens read-only, and what to do about it: the CLI's own words for the
// detection's "confirm the format" (the GUI's are the menu's).
static QString unconfirmedReason(const Detect::Result &r) {
    if (r.ranked.isEmpty()) return QString();
    const Detect::Candidate &c = r.ranked.first();
    if (!c.fromLibrary) return "the format is not in the library; see the definition under info";
    const QString &origin = c.fmt.origin;
    if (origin == "built-in" || origin == "cpmtools" || origin.isEmpty())
        return QString("the format is not confirmed; use --format=%1").arg(c.fmt.name);
    return QString("the format is not confirmed; use --diskdefs=%1 --format=%2").arg(origin, c.fmt.name);
}

class Image : public OpenedImage {
public:
    Cpm::OpenedDisc od;
    Detect::Result det;
    QString charset = "ascii";

    dw::fs::Disc &disc() override { return od.disc(); }
    bool save(bool backup, QString *err) override { return od.save(err, backup); }
    QString volumeNote(int) const override {
        return !det.ranked.isEmpty() && !det.ranked.first().fromLibrary ? QString("detected - see info") : QString();
    }
    QString readOnlyWhy() const override { return det.safeToWrite ? QString() : unconfirmedReason(det); }

    QString volName(int mount) const override {
        const auto &m = const_cast<Image *>(this)->od.disc().mounts();
        if (m.size() == 1) return m[0].vol && m[0].vol->fsId() == "torch" ? "torch" : "cpm";
        return OpenedImage::volName(mount);
    }

    QJsonObject app() const override {
        QJsonObject o;
        const Cpm::Format &f = const_cast<Image *>(this)->od.disc().mounts().isEmpty()
                                   ? Cpm::Format() : static_cast<Cpm::CpmVolume *>(const_cast<Image *>(this)->od.disc().mounts()[0].vol.get())->cpm()->format();
        QJsonObject fmt;
        fmt["name"] = f.name;
        fmt["description"] = f.desc;
        fmt["origin"] = f.origin;
        fmt["summary"] = f.summary();
        fmt["diskdef"] = f.toDiskdef();
        o["format"] = fmt;
        o["fromLibrary"] = !det.ranked.isEmpty() && det.ranked.first().fromLibrary;
        const auto &mounts = const_cast<Image *>(this)->od.disc().mounts();
        o["kind"] = !mounts.isEmpty() && mounts[0].vol ? mounts[0].vol->fsId() : QString("cpm");   // "torch" or "cpm"
        o["os"] = Cpm::osName(f.os);
        o["separateSides"] = f.separateSides;
        o["confidence"] = det.confidence;
        o["safeToWrite"] = det.safeToWrite;
        o["why"] = det.why;
        o["physical"] = det.physical;
        o["charset"] = charset;
        const auto &ms = const_cast<Image *>(this)->od.disc().mounts();
        bool lower = !ms.isEmpty() && static_cast<Cpm::CpmVolume *>(ms[0].vol.get())->lowerNames();
        o["lowerCaseNames"] = lower;
        return o;
    }

    // The character set for names and text, and lower-case names kept in the case given when the disc holds any
    // (as the main window sets them when it opens a disc).
    void setCharset(const QString &id) override {
        charset = Nat::find(id).id;
        for (const auto &m : od.disc().mounts()) {
            auto *v = static_cast<Cpm::CpmVolume *>(m.vol.get());
            v->setCharset(charset, Nat::usesLowerCase({v->cpm()}));
        }
    }

    bool convertTo(const QString &out, const ConvertOptions &o, QJsonObject *result, QString *err, int *exit) override {
        using dw::device::Kind;
        if (!o.toSides.isEmpty()) { *err = "a CP/M disc has no side order to change"; *exit = ExitUsage; return false; }
        Kind to = od.container().kind();
        if (o.container == "raw") to = Kind::Raw;
        else if (o.container == "imd") to = Kind::Imd;
        else if (o.container == "dsk") to = Kind::Dsk;
        else if (o.container == "edsk") to = Kind::Edsk;
        else if (o.container == "td0") to = Kind::Td0;
        if (to == Kind::Td0) { *err = "TeleDisk files can be read but not written: name --container=raw, imd, dsk or edsk"; *exit = ExitUnsupported; return false; }
        dw::device::Container x = od.container();
        x.discardPending();
        QVector<Cpm::Format> fmts;
        for (const auto &m : od.disc().mounts()) fmts << static_cast<Cpm::CpmVolume *>(m.vol.get())->cpm()->format();
        auto verify = [fmts](dw::device::Container &written, QString *e) {
            for (Cpm::Format f : fmts) {
                f.derived = false;
                const std::unique_ptr<Cpm::Store> v = Cpm::openStore(&written, f, e);
                if (!v) { *e = "the copy can't be read back: " + *e; return false; }
                const Cpm::Report r = v->check();
                if (!r.ok()) { *e = "the copy fails its check: " + r.errors.join("; "); return false; }
            }
            return true;
        };
        if (!x.saveAs(out, to, err, verify)) { *exit = ExitSave; return false; }
        QJsonObject r;
        r["container"] = dw::device::kindName(to);
        r["size"] = double(QFileInfo(out).size());
        *result = r;
        return true;
    }
};

// ------------------------------------------------------------------------------------------------ the Backend

dw::fs::OtherApp Backend::self() const { return dw::fs::cpmDiskWrangler(); }

bool Backend::loadDiskdefs(const QStringList &files, QString *err, int *exit) {
    if (files == m_loaded) return true;
    for (const QString &f : files)
        if (!QFileInfo::exists(f)) { *err = f + ": no such diskdefs file"; *exit = ExitNotFound; return false; }
    QStringList warnings;
    Cpm::setExtraDiskdefFiles(files, &warnings);
    m_loaded = files;
    return true;
}

static const Nat::Set *charsetNamed(const QString &id) {
    for (const Nat::Set &s : Nat::sets()) if (s.id == id) return &s;
    return nullptr;
}

OpenOutcome Backend::open(const QString &path, const OpenOptions &o) {
    OpenOutcome out;
    QString err;
    int code = ExitFailed;
    if (!o.charset.isEmpty() && !charsetNamed(o.charset)) {
        QStringList ids;
        for (const Nat::Set &s : Nat::sets()) ids << s.id;
        out.status = OpenOutcome::Usage;
        out.message = QString("--charset: unknown set '%1' (%2)").arg(o.charset, ids.join(", "));
        return out;
    }
    if (!loadDiskdefs(o.diskdefs, &err, &code)) { out.status = code == ExitNotFound ? OpenOutcome::NotFound : OpenOutcome::Failed; out.message = err; return out; }
    dw::device::Container c;
    if (!c.open(path, &err)) { out.status = OpenOutcome::Failed; out.message = path + ": " + err; return out; }
    out.container = containerInfo(path, c);

    Detect::Result r;
    if (o.format.isEmpty()) {
        r = Detect::detect(c);
    } else {
        // a named format: read the disc as it says; the reading is confirmed (nothing to be unsure of)
        const Cpm::Format *lib = Cpm::findFormat(o.format);
        if (!lib) { out.status = OpenOutcome::Usage; out.message = QString("--format: no format called '%1' (cpmdw new --list)").arg(o.format); return out; }
        Cpm::Format f = *lib;
        if (!f.derived && !f.derive(&err)) { out.status = OpenOutcome::Failed; out.message = err; return out; }
        if (c.kind() == dw::device::Kind::Raw) {
            dw::device::RawLayout l;
            l.seclen = f.seclen; l.sectrk = f.sectrk; l.heads = f.physHeads; l.cyls = f.cylinders;
            if (!c.setRawLayout(l, &err)) { out.status = OpenOutcome::Failed; out.message = QString("--format %1: %2").arg(o.format, err); return out; }
        } else {
            const dw::device::Shape sh = c.shape();
            if (f.seclen != sh.seclen || f.sectrk != sh.sectrk) {
                out.status = OpenOutcome::Failed;
                out.message = QString("--format %1: this disc's tracks hold %2 x %3-byte sectors").arg(o.format).arg(sh.sectrk).arg(sh.seclen);
                return out;
            }
            if (f.physHeads > c.heads()) { out.status = OpenOutcome::Failed; out.message = QString("--format %1: this disc has only one side").arg(o.format); return out; }
        }
        Detect::Candidate cand = Detect::evaluate(c, f, true);
        if (cand.score < -1e8) { out.status = OpenOutcome::Failed; out.message = QString("--format %1: %2").arg(o.format, cand.evidence.join("; ")); return out; }
        cand.fmt = f;
        cand.fromLibrary = true;
        cand.raw = c.rawLayout();
        cand.evidence << "the format was named with --format";
        r.ranked << cand;
        r.confidence = "high";
        r.safeToWrite = true;
        r.why = "the format was named on the command line";
        r.physical = dw::device::kindName(c.kind());
    }
    const dw::fs::Verdict v = Cpm::verdict(c, path, r);
    resolveVerdict(v, o.fs, &out);
    if (out.status != OpenOutcome::Ok) return out;
    const dw::fs::Candidate &cand = v.ranked[out.chosen];
    if (!cand.supported) { out.status = OpenOutcome::Unsupported; out.message = cand.description + ": this program can't open it"; return out; }
    if (r.ranked.isEmpty()) { out.status = OpenOutcome::Unrecognised; out.message = "no CP/M reading of this disc"; return out; }
    std::unique_ptr<Image> img(new Image);
    img->det = r;
    if (!r.safeToWrite) img->od.setReadOnly(unconfirmedReason(r));
    if (!img->od.open(path, r.ranked.first(), &err)) { out.status = OpenOutcome::Failed; out.message = err; return out; }
    img->setCharset(o.charset.isEmpty() ? QString("ascii") : o.charset);
    out.image = std::move(img);
    return out;
}

QJsonArray Backend::formats() const {
    QJsonArray a;
    for (const dw::fs::Preset &p : Cpm::libraryPresets()) {     // the library's floppy formats, from the presets registry
        QJsonObject o;
        o["id"] = p.id;
        o["name"] = p.name;
        o["description"] = p.notes;
        o["os"] = p.extra.value("os").toString();
        o["doubleSided"] = p.doubleSided;
        o["separateSidesOk"] = p.extra.value("separateSidesOk").toBool();
        o["summary"] = p.extra.value("summary").toString();
        o["origin"] = p.extra.value("origin").toString();
        a.append(o);
    }
    return a;
}

bool Backend::create(const QString &file, const NewOptions &n, QString *err, int *exit) {
    using dw::device::Kind;
    const Cpm::Format *lib = Cpm::findFormat(n.format);
    if (!lib || lib->offsetSectors) { *err = QString("unknown format '%1' (cpmdw new --list)").arg(n.format); *exit = ExitUsage; return false; }
    Kind kind = Kind::Imd;
    if (n.container == "raw") kind = Kind::Raw;
    else if (n.container == "imd" || n.container.isEmpty()) kind = Kind::Imd;
    else if (n.container == "dsk") kind = Kind::Dsk;
    else if (n.container == "edsk") kind = Kind::Edsk;
    else { *err = "--container is raw, imd, dsk or edsk"; *exit = n.container == "td0" ? ExitUnsupported : ExitUsage; return false; }
    Cpm::Format f = *lib;
    if (n.separateSides) {
        Cpm::Format g;
        if (!Cpm::separateSidesVariant(*lib, &g, err)) { *exit = ExitUnsupported; return false; }
        f = g;
    }
    if (!n.label.isEmpty() && !n.stamps) { *err = "--label needs --stamps (a label belongs to a CP/M 3 disc with time stamps)"; *exit = ExitUsage; return false; }
    dw::device::Container img;
    if (!Cpm::makeBlank(&img, kind, f, n.stamps, n.label.toUpper(), err)) { *exit = ExitFailed; return false; }
    const QString tmp = tempBeside(file);
    if (!img.saveAs(tmp, kind, err)) { QFile::remove(tmp); *exit = ExitSave; return false; }
    QFile::remove(file);
    if (!QFile::rename(tmp, file)) { *err = "cannot write " + file; QFile::remove(tmp); *exit = ExitSave; return false; }
    return true;
}

std::shared_ptr<dw::xfer::HostPolicy> Backend::hostPolicy(const GetOptions &o, QString *, int *) {
    return std::make_shared<RetroFiles::CpmPolicy>(o.userFolders == "on", o.trim);
}

// The core's host reader with this program's rules (RetroFiles::cpmHostRead: the .inf rule, 16 MB). The run stops at
// a file that can't be read or isn't there, else at a file too large, with the words cpmdw has always used.
bool Backend::readHost(const QStringList &paths, const PutOptions &, QVector<dw::TransferItem> *items, QStringList *, QString *err, int *exit) {
    QVector<dw::xfer::HostReadError> errors;
    const QVector<dw::TransferItem> got = dw::xfer::readHost(paths, RetroFiles::cpmHostRead(), &errors);
    QVector<dw::xfer::HostReadError> reading, tooLarge;
    for (const dw::xfer::HostReadError &e : errors) (e.kind == dw::xfer::HostReadError::TooLarge ? tooLarge : reading) << e;
    if (hostReadFailed(reading, err, exit)) return false;
    if (!tooLarge.isEmpty()) { *err = QFileInfo(tooLarge.first().path).fileName() + ": too large for a CP/M disc"; *exit = ExitFailed; return false; }
    *items += got;
    return true;
}

// ------------------------------------------------------------------------------------------------ before importing

static void setUser(dw::TransferItem *t, int user) {
    QJsonObject c = t->attrs.value("cpm");
    c["user"] = user;
    t->attrs["cpm"] = c;
}

// A host folder called userN (N within the disc's range) is that user's files: its files go in user N and the folder is
// not made. Other folders are left for the importer to flatten, with a warning.
static QVector<dw::TransferItem> placeUsers(const QVector<dw::TransferItem> &in, int override, int maxUser) {
    QVector<dw::TransferItem> level = in;
    RetroFiles::withoutClashSuffixes(&level);
    QVector<dw::TransferItem> out;
    for (dw::TransferItem x : level) {
        if (x.isDir) {
            int u = RetroFiles::userFromFolderName(x.name);
            if (u > maxUser) u = -1;
            const int use = u >= 0 ? u : override;
            QVector<dw::TransferItem> kids = placeUsers(x.kids, use, maxUser);
            if (u >= 0) { out += kids; continue; }
            x.kids = kids;
            out.append(x);
            continue;
        }
        if (override >= 0) setUser(&x, override);
        out.append(x);
    }
    return out;
}

void Backend::prepareIncoming(QVector<dw::TransferItem> *items, dw::fs::Volume *vol) {
    auto *cv = dynamic_cast<Cpm::CpmVolume *>(vol);
    if (!cv) return;
    const int maxUser = cv->cpm()->format().os == Cpm::Os::V3 ? 15 : 31;
    // only host items are read for folders and suffixes: items from other discs keep their names and users
    *items = placeUsers(*items, -1, maxUser);
}

} // namespace CpmCli
