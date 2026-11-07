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
#include "cpmvolume.h"

#include <QFileInfo>

using dw::fs::AttrSpec;
using dw::fs::AttrValues;
using dw::fs::NodeKind;
using dw::fs::Path;

namespace Cpm {

bool CpmNameRules::valid(const QString &name, NodeKind kind, QString *why) const {
    if (kind == NodeKind::Dir) { if (why) *why = "CP/M has no directories"; return false; }
    if (kind == NodeKind::Namespace) {
        const int u = CpmVolume::userOf(name);
        if (u < 0 || u > 15) { if (why) *why = "a user area is \"user 0\" to \"user 15\""; return false; }
        return true;
    }
    return Volume::validName(name, why, *m_extra);
}

QString CpmNameRules::fromHost(const QString &host, NodeKind) const { return Volume::toCpmName(host, *m_extra); }

int CpmVolume::userOf(const QString &level) {
    if (!level.startsWith("user ", Qt::CaseInsensitive)) return -1;
    bool ok = false;
    const int u = level.mid(5).toInt(&ok);
    return ok && u >= 0 && u <= 31 ? u : -1;
}

dw::fs::Caps CpmVolume::caps() const {
    dw::fs::Caps c;
    c.namespaces = true;
    c.renameAcrossDirs = true;           // to another user
    c.title = m_v->labelAllowed();
    c.dates = m_v->hasStamps();
    c.maxEntriesPerDir = m_v->format().maxdir;
    return c;
}

QVector<AttrSpec> CpmVolume::fileAttrs() const {
    QVector<AttrSpec> v;
    const QList<QPair<QString, QString>> bools = {{"readonly", "R/O"}, {"system", "SYS"}, {"archive", "ARC"},
                                                  {"f1", "F1"}, {"f2", "F2"}, {"f3", "F3"}, {"f4", "F4"}};
    for (const auto &b : bools) {
        AttrSpec a;
        a.key = b.first;
        a.label = b.second;
        a.type = AttrSpec::Bool;
        a.sample = "R/O";
        a.column = a.key.size() > 2;      // F1-F4 only in Properties
        a.retroFamily = "cpm";
        a.retroKey = b.first;
        v << a;
    }
    return v;
}

QVector<AttrSpec> CpmVolume::volumeAttrs() const {
    if (!m_v->labelAllowed()) return {};
    AttrSpec l;
    l.key = "label"; l.label = "Label"; l.type = AttrSpec::Text; l.sample = "MMMMMMMMMMM";
    return {l};
}

AttrValues CpmVolume::volumeValues() const {
    return m_v->labelAllowed() ? AttrValues{{"label", m_v->label().name}} : AttrValues();
}

dw::fs::VolumeInfo CpmVolume::info() const {
    dw::fs::VolumeInfo i;
    i.title = m_v->label().name;
    const Format &f = m_v->format();
    i.total = qint64(f.dsm + 1) * f.blocksize;
    i.free = m_v->freeBytes();
    i.entriesFree = m_v->freeEntries();
    return i;
}

quint16 CpmVolume::attrBits(const AttrValues &a, quint16 base) {
    auto bit = [&](const char *k, quint16 f) {
        if (a.contains(k)) base = a.value(k).toBool() ? quint16(base | f) : quint16(base & ~f);
    };
    bit("readonly", AttrRO);
    bit("system", AttrSYS);
    bit("archive", AttrARC);
    bit("f1", AttrF1);
    bit("f2", AttrF2);
    bit("f3", AttrF3);
    bit("f4", AttrF4);
    return base;
}

static AttrValues attrValues(quint16 a) {
    return {{"readonly", bool(a & AttrRO)}, {"system", bool(a & AttrSYS)}, {"archive", bool(a & AttrARC)},
            {"f1", bool(a & AttrF1)},       {"f2", bool(a & AttrF2)},       {"f3", bool(a & AttrF3)},
            {"f4", bool(a & AttrF4)}};
}

bool CpmVolume::userOfDir(const Path &dir, int *user, QString *err) const {
    if (dir.parts.size() != 1 || (*user = userOf(dir.parts[0])) < 0) {
        *err = "not a user area: " + dir.toString();
        return false;
    }
    return true;
}

bool CpmVolume::find(const Path &p, int *idx, QString *err) const {
    int u;
    if (p.parts.size() != 2 || !userOfDir(p.parent(), &u, err)) { *err = "no such file: " + p.toString(); return false; }
    *idx = m_v->indexOf(u, p.name());
    if (*idx < 0) { *err = "no such file: " + p.toString(); return false; }
    return true;
}

bool CpmVolume::list(const Path &dir, QVector<dw::fs::Entry> *out, QString *err) const {
    out->clear();
    if (dir.isRoot()) {
        for (int u : m_v->usersInUse()) {
            dw::fs::Entry e;
            e.name = userName(u);
            e.kind = NodeKind::Namespace;
            out->append(e);
        }
        return true;
    }
    int u;
    if (!userOfDir(dir, &u, err)) return false;
    for (const File &f : m_v->files()) {
        if (f.user != u) continue;
        dw::fs::Entry e;
        e.name = f.name;
        e.size = f.size;
        e.sizeExact = f.exactSize;
        e.created = f.created;
        e.modified = f.updated.isValid() ? f.updated : f.created;
        e.accessed = f.accessed;
        e.attrs = attrValues(f.attrs);
        e.problem = f.problem;
        out->append(e);
    }
    return true;
}

bool CpmVolume::read(const Path &file, QByteArray *out, QString *err) const {
    int i;
    return find(file, &i, err) && m_v->readFile(m_v->files()[i], out, err);
}

bool CpmVolume::write(const Path &dir, const QString &name, const QByteArray &data, const AttrValues &a,
                      const QDateTime &when, bool replace, QString *err) {
    int u;
    return userOfDir(dir, &u, err) && m_v->addFile(u, name, data, attrBits(a), when, replace, err);
}

bool CpmVolume::mkdir(const Path &dir, const QString &name, QString *err) {
    if (!dir.isRoot() || userOf(name) < 0 || userOf(name) > 15) { *err = "CP/M has only user areas 0-15"; return false; }
    return true;                         // a user area exists as soon as a file is in it
}

bool CpmVolume::remove(const Path &node, QString *err) {
    if (node.parts.size() == 1) {        // a whole user area
        int u;
        if (!userOfDir(node, &u, err)) return false;
        QStringList names;
        for (const File &f : m_v->files()) if (f.user == u) names << f.name;
        for (const QString &n : names) if (!m_v->deleteFile(u, n, err)) return false;
        return true;
    }
    int i;
    if (!find(node, &i, err)) return false;
    const File f = m_v->files()[i];
    return m_v->deleteFile(f.user, f.name, err);
}

bool CpmVolume::rename(const Path &from, const Path &to, QString *err) {
    int i, u;
    if (!find(from, &i, err)) return false;
    if (to.parts.size() != 2 || !userOfDir(to.parent(), &u, err)) { *err = "not a file in a user area: " + to.toString(); return false; }
    const File f = m_v->files()[i];
    return m_v->rename(f.user, f.name, u, to.name(), err);
}

bool CpmVolume::setAttrs(const Path &node, const AttrValues &a, QString *err) {
    int i;
    if (!find(node, &i, err)) return false;
    const File f = m_v->files()[i];
    return m_v->setAttrs(f.user, f.name, attrBits(a, f.attrs), err);
}

bool CpmVolume::setVolumeAttrs(const AttrValues &a, QString *err) {
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (it.key() != "label") { *err = "CP/M discs have no " + it.key(); return false; }
        if (!m_v->setLabel(it.value().toString(), err)) return false;
    }
    return true;
}

dw::fs::Report CpmVolume::check() const {
    const Report r = m_v->check();
    dw::fs::Report o;
    o.errors = r.errors;
    o.warnings = r.warnings;
    return o;
}

QMap<QString, QJsonObject> CpmVolume::toRetro(const dw::fs::Entry &e) const {
    QMap<QString, QJsonObject> m = dw::fs::Volume::toRetro(e);
    QJsonObject c = m.value("cpm");               // as the drag-out writes it: f1-f4 only when set, and the user
    for (const char *k : {"f1", "f2", "f3", "f4"}) if (!c.value(k).toBool()) c.remove(k);
    m["cpm"] = c;
    return m;
}

dw::fs::Placement CpmVolume::placeIncoming(const dw::TransferItem &item, const Path &targetDir) const {
    dw::fs::Placement p = dw::fs::Volume::placeIncoming(item, targetDir);
    QString name = item.name;
    if (item.sourceFs == "dfs" && name.size() >= 3 && name[1] == '.') name = name.mid(2);   // "D.NAME": drop D
    p.name = m_names.fromHost(name, NodeKind::File);
    const QJsonObject c = item.attrs.value("cpm");
    if (c.contains("user")) p.dir = Path({userName(c.value("user").toInt())});
    else if (targetDir.isRoot()) p.dir = Path({userName(0)});
    return p;
}

// ------------------------------------------------------------------ OpenedDisc

bool OpenedDisc::open(const QString &path, QString *err) {
    if (!m_img.open(path, err)) return false;
    m_det = Detect::detect(m_img);
    if (m_det.ranked.isEmpty()) { *err = m_det.why; return false; }
    return attach(m_det.ranked.first(), err);
}

bool OpenedDisc::open(const QString &path, const Detect::Candidate &c, QString *err) {
    return m_img.open(path, err) && attach(c, err);
}

bool OpenedDisc::attach(const Detect::Candidate &c, QString *err) {
    if (m_img.kind() == dw::device::Kind::Raw && c.raw.valid() && !m_img.setRawLayout(c.raw, err)) return false;
    m_disc.clearMounts();
    m_disc.setContainer(&m_img);
    const int n = c.fmt.separateSides ? 2 : 1;
    for (int side = 0; side < n; ++side) {
        Format f = c.fmt;
        f.fixedHead = side;
        f.derived = false;
        if (!m_vols[side].attach(&m_img, f, err)) { if (n > 1) *err = QString("side %1: %2").arg(side).arg(*err); return false; }
        m_disc.addMount({n > 1 ? QString("side %1").arg(side) : QString("CP/M"), std::make_shared<CpmVolume>(&m_vols[side]),
                         false, QString(), "cpm"});
    }
    return true;
}

bool OpenedDisc::save(QString *err, bool backup) {
    dw::device::Container::SaveOptions o;
    o.backup = backup;
    // the copy as written: each side's volume attached to it and checked (MainWindow::checkWritten does the same)
    o.verify = [this](dw::device::Container &written, QString *e) {
        for (int side = 0; side < m_disc.mounts().size(); ++side) {
            Volume v;
            if (!v.attach(&written, m_vols[side].format(), e)) return false;
            const Report r = v.check();
            if (!r.ok()) { *e = "the written copy fails its check: " + r.errors.join("; "); return false; }
        }
        return true;
    };
    return m_disc.commit(o, err);
}

// ------------------------------------------------------------------ detection by tiers

QVector<dw::fs::Candidate> CpmProbe::probe(const dw::fs::ProbeContext &) const {
    if (m_r.ranked.isEmpty()) return {};
    const Detect::Candidate &best = m_r.ranked.first();
    dw::fs::Candidate c;
    c.fsId = best.fmt.torch ? "torch" : "cpm";
    c.name = best.fmt.torch ? "Torch CP/N" : "CP/M";
    c.description = QString("CP/M disc (%1)").arg(best.fmt.name);
    c.tier = m_r.confidence == "high" || best.remembered ? dw::fs::Tier::Certain
             : best.files > 0 ? dw::fs::Tier::Likely : dw::fs::Tier::Possible;
    c.score = best.score;
    c.evidence = best.evidence;
    c.notes << m_r.why;
    c.safeToWrite = m_r.safeToWrite;
    c.data = m_r;
    return {c};
}

QVector<dw::fs::Candidate> AcornRecogniser::probe(const dw::fs::ProbeContext &ctx) const {
    if (!ctx.container) return {};
    const QString what = Detect::otherFilesystem(*ctx.container);
    if (what.isEmpty()) return {};
    dw::fs::Candidate c;
    c.fsId = "acorn";
    c.name = "Acorn";
    c.description = what;
    c.tier = dw::fs::Tier::Likely;
    c.owner = dw::fs::acornDiskWrangler();
    c.evidence << what;
    return {c};
}

dw::fs::Verdict verdict(dw::device::Container &img, const QString &path, const Detect::Result &r) {
    dw::fs::ProbeContext ctx;
    ctx.path = path;
    ctx.suffix = QFileInfo(path).suffix().toLower();
    ctx.container = &img;
    const CpmProbe cpm(r);
    const AcornRecogniser acorn;
    // CP/M first: of two readings of the same tier, its own is preferred - but two Likely ones are asked about
    return dw::fs::detect(ctx, {&cpm, &acorn});
}

} // namespace Cpm
