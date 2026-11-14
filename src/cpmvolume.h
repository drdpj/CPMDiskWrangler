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
// cpmvolume.h - CP/M Disk Wrangler on DiskWranglerCore's filesystem interface (step 4 of the move to the core):
// a CP/M (or Torch CP/N) volume as a dw::fs::Volume, the opened disc as a dw::fs::Disc (one volume, or one per side),
// and detection by tiers: CP/M's own scored detection as one probe, and a recogniser for the discs that are Acorn
// Disk Wrangler's. The main window (on the core's window kit since step 5) and the command-line program both use
// them: OpenedDisc is the window's document and cpmdw's opened image. A Torch CP/N disc is a TorchVolume over a
// Torch::Fs (step 5b): the same CP/M names, users and attributes as CpmVolume, its own filesystem underneath.
#pragma once

#include <memory>

#include <diskwrangler/app/appinfo.h>
#include <diskwrangler/app/document.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/volume.h>

#include "cpmfs.h"
#include "detect.h"
#include "natsets.h"

namespace Cpm {

class CpmNameRules : public dw::fs::NameRules {
public:
    explicit CpmNameRules(const QString *extra) : m_extra(extra) {}
    bool valid(const QString &name, dw::fs::NodeKind kind, QString *why) const override;
    QString fromHost(const QString &hostName, dw::fs::NodeKind kind) const override;
private:
    const QString *m_extra;                       // the national letters allowed (natchars.h)
};

// User areas are namespace levels: a file's path is {"user 0", "NAME.EXT"}; the root lists the users in use as
// Namespace entries ("user 0" ... "user 15"). Attributes: readonly, system, archive, f1-f4 (retro-files "cpm",
// with the user number). Volume attribute: the label (where the format allows one). Over any Cpm::Store; a CP/M
// directory's is a Cpm::Volume (TorchVolume, below, is the one for Torch CP/N).
class CpmVolume : public dw::fs::Volume {
public:
    explicit CpmVolume(Cpm::Store *v, QString extraNameChars = QString())
        : m_v(v), m_extra(std::move(extraNameChars)), m_names(&m_extra) {}
    Cpm::Store *cpm() const { return m_v; }
    static QString userName(int user) { return QString("user %1").arg(user); }
    static int userOf(const QString &level);      // "user 3" -> 3, -1 if not a user level

    // The national character set (natsets.h) names are shown and typed in, and whether the disc keeps lower-case names
    // (lower: names are kept in the case given). Entry names stay as the disc holds them; displayPath, hostNameFor and
    // placeIncoming convert, as the main window does. Also sets the volume's name rules.
    void setCharset(const QString &id, bool lower);
    const Nat::Set &charset() const { return m_set; }
    bool lowerNames() const { return m_lower; }

    QString fsId() const override { return "cpm"; }
    dw::fs::Caps caps() const override;
    QVector<dw::fs::AttrSpec> fileAttrs() const override;
    bool marksInexactSizes() const override { return false; }     // as in the program's own list: the plain size
    QVector<dw::fs::AttrSpec> volumeAttrs() const override;
    dw::fs::AttrValues volumeValues() const override;
    const dw::fs::NameRules &names() const override { return m_names; }
    dw::fs::VolumeInfo info() const override;

    // CP/M paths: "0:NAME", "NAME" (user 0), "3:" (a user area), "" or "/" (the root: the users); shown as "0:NAME".
    // Names are typed in the disc's character set.
    bool parsePath(const QString &text, dw::fs::Path *out, QString *err) const override;
    QString displayPath(const dw::fs::Path &p) const override;
    QString hostNameFor(const dw::fs::Entry &e) const override;

    bool list(const dw::fs::Path &dir, QVector<dw::fs::Entry> *out, QString *err) const override;
    bool read(const dw::fs::Path &file, QByteArray *out, QString *err) const override;
    bool write(const dw::fs::Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs,
               const QDateTime &when, bool replace, QString *err) override;
    bool mkdir(const dw::fs::Path &dir, const QString &name, QString *err) override;   // a user area: nothing to make
    bool remove(const dw::fs::Path &node, QString *err) override;
    bool rename(const dw::fs::Path &from, const dw::fs::Path &to, QString *err) override;  // also to another user
    bool setAttrs(const dw::fs::Path &node, const dw::fs::AttrValues &a, QString *err) override;
    bool setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) override;
    dw::fs::Report check() const override;
    bool reload(QString *err) override { return m_v->reload(err); }

    QMap<QString, QJsonObject> toRetro(const dw::fs::Entry &e) const override;
    QMap<QString, QJsonObject> toRetroIn(const dw::fs::Path &dir, const dw::fs::Entry &e) const override;   // with "user"
    // An item copied in goes in the user area of its retro-files "cpm" user, if it has one, else the target's.
    dw::fs::Placement placeIncoming(const dw::TransferItem &item, const dw::fs::Path &targetDir) const override;

    static quint16 attrBits(const dw::fs::AttrValues &a, quint16 base = 0);
    static dw::fs::AttrValues attrValues(quint16 bits);        // every attribute, as the bits say

private:
    bool userOfDir(const dw::fs::Path &dir, int *user, QString *err) const;
    bool find(const dw::fs::Path &p, int *idx, QString *err) const;
    void applyRules();
    Cpm::Store *m_v;
    QString m_extra;
    CpmNameRules m_names;
    Nat::Set m_set = Nat::find("ascii");
    bool m_lower = false;
};

// Torch CP/N (a Torch::Fs): CP/M files with CpmVolume's names, user areas, attributes, character set and placement,
// in Torch's own filesystem. Up to 256 files in all, no label (and no other volume attribute), sizes in 256-byte
// sectors.
class TorchVolume : public CpmVolume {
public:
    explicit TorchVolume(Torch::Fs *t, QString extraNameChars = QString()) : CpmVolume(t, std::move(extraNameChars)), m_t(t) {}
    Torch::Fs *torch() const { return m_t; }
    QString fsId() const override { return "torch"; }
    dw::fs::Caps caps() const override;
    QVector<dw::fs::AttrSpec> volumeAttrs() const override { return {}; }
    dw::fs::AttrValues volumeValues() const override { return {}; }
    bool setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) override;   // refused: no label
    dw::fs::VolumeInfo info() const override;
private:
    Torch::Fs *m_t;
};

// The opened disc: the container and its volumes, attached as detection's candidate says (a TorchVolume for a Torch
// CP/N candidate, else a CpmVolume per side). It is the window's Document (step 5c) and the console program's opened
// image alike, so both save through the same checks: every side's directory consistent, the file not changed by
// something else since it was opened, and the copy as written read back with the same number of files.
class OpenedDisc : public dw::app::Document {
public:
    OpenedDisc() = default;
    OpenedDisc(const OpenedDisc &) = delete;
    OpenedDisc &operator=(const OpenedDisc &) = delete;
    bool open(const QString &path, QString *err);                   // detection's best reading
    bool open(const QString &path, const Detect::Candidate &c, QString *err);
    // The window's open (step 5g): the file's container opened and its state noted, no reading attached yet. The
    // window runs detection, the kit's open pipeline decides (ask, hand over), and the window attaches the reading
    // chosen. A file that can't be opened leaves the document as it was.
    bool load(const QString &path, QString *err);
    // The window runs detection itself (Tools > Disc format re-reads it): attach a reading of the container as it is,
    // or detach every volume (the container stays open). attach's err names the side ("side
    // 1: ...") when the format has a filesystem on each side.
    bool attach(const Detect::Candidate &c, QString *err);
    void detach();
    // The file's date and size now: a save refuses to write over the file if either has changed since (open() and a
    // save note them; the window notes them after opening or making a container itself).
    void noteFileState();
    dw::fs::Disc &disc() override { return m_disc; }
    const dw::fs::Disc &disc() const override { return m_disc; }
    dw::device::Container &container() { return m_img; }
    const dw::device::Container &container() const { return m_img; }
    const Detect::Result &detection() const { return m_det; }
    int sides() const { return m_disc.mounts().size(); }
    Store *store(int side) const { return side >= 0 && side < 2 ? m_stores[side].get() : nullptr; }
    // The side as a Cpm::Volume: the store itself for a CP/M directory, the facade over it for a Torch CP/N disc (for
    // the window's code that still takes a Cpm::Volume, and its tests; removed with the facade in step 6). A detached
    // Volume when there is no such side.
    Volume &legacy(int side);
    bool save(QString *err, bool backup = false) { return save(backup, err); }
    // Call before open(path, candidate): the mounts are read-only, for this reason (the format isn't confirmed).
    void setReadOnly(const QString &why) { m_roWhy = why; }

    // ----- the window's Document
    bool isOpen() const override { return m_img.isOpen(); }
    QString path() const override { return m_img.path(); }
    bool dirty() const override { return m_img.hasPending(); }
    bool save(bool backup, QString *err) override;
    // Save As: the copy is checked as save() checks it, unless the directory already has errors (a TeleDisk image
    // that fails its check can still be copied to a writable kind, as it is).
    bool saveAs(const QString &path, dw::device::Kind kind, QString *err) override;
    // The window's reasons, the same for every side: a TeleDisk image, a directory with errors, a format not
    // confirmed (setSafe(false)); then the mount's own (setReadOnly).
    QString readOnlyWhy(int mount) const override;
    void setSafe(bool on) { m_safe = on; }        // detection (or the user) says the format can be written with
    bool safe() const { return m_safe; }

private:
    bool checkWritten(dw::device::Container &written, QString *err) const;
    dw::device::Container m_img;
    Detect::Result m_det;
    std::unique_ptr<Store> m_stores[2];               // the filesystem on each side (openStore); outlive the mounts
    Volume m_facades[2];                              // legacy(): a Torch::Fs side's facade
    QString m_roWhy;
    bool m_safe = true, m_checkOk = true;
    QDateTime m_fileTime;
    qint64 m_fileSize = -1;
    dw::fs::Disc m_disc;
};

// CP/M Disk Wrangler, for the window and the console program (version: the caller's).
dw::app::AppInfo appInfo(const QString &version);

// ----- detection by tiers
// CP/M's scored detection as a probe: one candidate ("cpm" or "torch") for the best reading, Certain when detection
// is confident (or the user confirmed the format), Likely when CP/M files were found, else Possible. data is the
// Detect::Result. The detection is run once, by the caller (it is the slow part).
class CpmProbe : public dw::fs::Probe {
public:
    explicit CpmProbe(const Detect::Result &r) : m_r(r) {}
    QString id() const override { return "cpm"; }
    QVector<dw::fs::Candidate> probe(const dw::fs::ProbeContext &ctx) const override;
private:
    Detect::Result m_r;
};

// The discs that are Acorn Disk Wrangler's (ADFS, DFS, PC and DOS Plus FAT; detect.cpp's foreignFilesystem): one
// candidate, Likely, owned by Acorn Disk Wrangler.
class AcornRecogniser : public dw::fs::Probe {
public:
    QString id() const override { return "acorn"; }
    QVector<dw::fs::Candidate> probe(const dw::fs::ProbeContext &ctx) const override;
};

// What to do with an opened container, given detection's result: open it (the CP/M reading), ask, or hand it to
// Acorn Disk Wrangler.
dw::fs::Verdict verdict(dw::device::Container &img, const QString &path, const Detect::Result &r);

} // namespace Cpm
