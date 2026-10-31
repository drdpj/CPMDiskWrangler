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
#include "formatdialog.h"
#include "cpmfs.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

// What the scores mean: kept next to the numbers in detect.cpp (scoreDir, scoreContents, the known-format bonuses and
// the confidence rules at the end of detect()). Change one, change the other.
static void showScoringHelp(QWidget *parent) {
    const QString html = QStringLiteral(
        "<h3>How the readings are scored</h3>"
        "<p>Each row is one way of laying a CP/M filesystem over this disc: sides, reserved tracks, skew, block size and "
        "directory size. The program reads the directory with that layout, checks what it finds and adds up points. A "
        "higher score means the layout explains the disc better. Only the differences between rows matter: a score is not "
        "a percentage, and scores are only comparable within one list, for one disc.</p>"
        "<h4>The directory (each entry in use)</h4>"
        "<ul>"
        "<li>+4 for each file entry, and +2 more when the number of block pointers matches the file's record count "
        "(-3 when it does not)</li>"
        "<li>-15 for an entry that makes no sense (control characters in a name, impossible counts)</li>"
        "<li>-8 for a block pointer outside the disc or inside the directory, for a block used by two entries, and for the "
        "same extent listed twice; -1 for each gap in a pointer list</li>"
        "<li>+6 when the lowest file block is the first one after the directory</li>"
        "<li>a little for a disc label or time-stamp entry</li>"
        "</ul>"
        "<h4>The files (the first 8K of up to 32 of them)</h4>"
        "<ul>"
        "<li>+3 for a text file that stays text up to its end mark, -8 for one that does not</li>"
        "<li>+1 for a .COM file that starts with a plausible instruction, -1 if not</li>"
        "<li>-4 for a file that starts in never-written (E5) space, -1 for each never-written sector inside a file, and -4 "
        "for a file of nothing but zeros. A wrong side order or reserved-track count sends file blocks to places that were "
        "never written. Real files have a few such sectors, so this only separates rival readings.</li>"
        "</ul>"
        "<h4>Known formats</h4>"
        "<ul>"
        "<li>+3 when the layout equals a built-in or cpmtools format; +4 if the disc's sector numbers start where that "
        "format's do, -6 if they do not</li>"
        "<li>+3 or -3 when the format has a track 0 of its own shape and the disc does or does not match it; +6 for the DFS "
        "catalogue Acorn's CP/M writes</li>"
        "<li>+15 when the Amstrad boot sector describes the disc; +20 for the format you chose for this disc before</li>"
        "<li>A format you type in by hand gets exactly the same points if it equals a known format.</li>"
        "</ul>"
        "<h4>Confidence, and whether the disc may be changed</h4>"
        "<p><b>High</b>: no other layout comes close. <b>Medium</b>: another reading lists the same files but would write "
        "new ones differently, or the best reading has inconsistencies. <b>Low</b>: another layout reads a different "
        "directory and scores within 6 points of the best.</p>"
        "<p>A reading has inconsistencies when it has nonsense entries, bad pointers, a block used twice, text files that "
        "stop being text, more than a quarter of its file entries whose pointer count disagrees with the records, or block "
        "pointers that look two bytes wide (see \"Two-byte block pointers\").</p>"
        "<p>The program only lets you change a disc when the reading is clean, the disc has no bad sectors, nothing "
        "competes with it, and the format is a known one or one you have confirmed with the box at the bottom.</p>");
    QDialog d(parent);
    d.setWindowTitle("How scoring works");
    auto *view = new QTextBrowser;
    view->setHtml(html);
    view->setOpenLinks(false);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *lay = new QVBoxLayout(&d);
    lay->addWidget(view);
    lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    d.resize(560, 560);
    d.exec();
}

FormatDialog::FormatDialog(Disk::Image *img, const Detect::Result &det, int currentIndex, bool writable, QWidget *parent)
    : QDialog(parent), m_img(img) {
    setWindowTitle("Disc format");
    resize(1120, 680);
    auto *phys = new QLabel("<b>Physical:</b> " + det.physical.toHtmlEscaped() + "<br><b>Detection:</b> " +
                            QString("confidence %1. ").arg(det.confidence) + det.why.toHtmlEscaped());
    phys->setWordWrap(true);
    m_list = new QTreeWidget;
    m_list->setHeaderLabels({"Score", "Format", "Files", "Layout"});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_list->setColumnWidth(2, fontMetrics().horizontalAdvance("Files  000"));
    m_list->setColumnWidth(0, fontMetrics().horizontalAdvance("00000.0  "));
    m_details = new QPlainTextEdit;
    m_details->setReadOnly(true);
    m_details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // ----- the hand-made format
    auto *form = new QGroupBox("Try a format by hand");
    m_seclen = new QComboBox; m_seclen->addItems({"128", "256", "512", "1024"});
    m_sectrk = new QSpinBox; m_sectrk->setRange(1, 255);
    m_tracks = new QSpinBox; m_tracks->setRange(1, 4096);
    m_heads = new QSpinBox; m_heads->setRange(1, 2);
    m_sides = new QComboBox; m_sides->addItems({"alt", "outback", "outout"});
    m_boot = new QSpinBox; m_boot->setRange(0, 64);
    m_skew = new QSpinBox; m_skew->setRange(0, 64);
    m_skewtab = new QLineEdit; m_skewtab->setPlaceholderText("or a table: 0,2,4,...");
    m_bls = new QComboBox; m_bls->addItems({"1024", "2048", "4096", "8192", "16384"});
    m_maxdir = new QSpinBox; m_maxdir->setRange(16, 8192);
    m_dirblks = new QSpinBox; m_dirblks->setRange(0, 64); m_dirblks->setSpecialValueText("auto");
    m_os = new QComboBox; m_os->addItems({"2.2", "3", "p2dos", "zsys"});
    m_ptr16 = new QCheckBox("Two-byte block pointers");
    m_ptr16->setToolTip("A directory entry holds block numbers: normally sixteen one-byte numbers, or eight two-byte numbers "
                        "on a disc of more than 256 blocks.\n"
                        "Tick this for a smaller disc that uses two-byte numbers anyway (the Tatung Einstein does).\n"
                        "A sign: read as one-byte numbers, the pointers come out as 1, 0, 2, 0, 3, 0 ... with a zero after "
                        "each block number.\n"
                        "It only changes how the directory is read; the disc's real size still limits the blocks.");
    m_formErr = new QLabel; m_formErr->setWordWrap(true);
    auto *fl = new QFormLayout;
    fl->addRow("Sector size", m_seclen);
    fl->addRow("Sectors per track", m_sectrk);
    fl->addRow("Tracks (all sides)", m_tracks);
    auto *hs = new QHBoxLayout; hs->addWidget(m_heads); hs->addWidget(new QLabel("order")); hs->addWidget(m_sides);
    fl->addRow("Sides", hs);
    fl->addRow("Boot tracks", m_boot);
    auto *sk = new QHBoxLayout; sk->addWidget(m_skew); sk->addWidget(m_skewtab, 1);
    fl->addRow("Skew", sk);
    fl->addRow("Block size", m_bls);
    fl->addRow("Directory entries", m_maxdir);
    fl->addRow("Directory blocks", m_dirblks);
    fl->addRow("CP/M version", m_os);
    fl->addRow(QString(), m_ptr16);
    auto *tryBtn = new QPushButton("Try these settings");
    auto *fv = new QVBoxLayout(form);
    fv->addLayout(fl);
    fv->addWidget(tryBtn);
    fv->addWidget(m_formErr);
    fv->addStretch();

    auto *right = new QSplitter(Qt::Vertical);
    right->addWidget(m_list);
    right->addWidget(m_details);
    auto *mid = new QSplitter;
    mid->addWidget(right);
    mid->addWidget(form);
    mid->setStretchFactor(0, 3);
    mid->setSizes({700, 300});
    m_list->setColumnWidth(1, fontMetrics().horizontalAdvance("ibm-3740 (chosen)  "));

    m_allow = new QCheckBox("Allow changes to the disc with this format (I have checked the file list)");
    m_allow->setChecked(writable);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Use this format");
    auto *copyBtn = bb->addButton("Copy as diskdef", QDialogButtonBox::ActionRole);
    auto *helpBtn = bb->addButton("How scoring works", QDialogButtonBox::HelpRole);
    helpBtn->setToolTip("What the scores mean, and how a reading is chosen");
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(phys);
    lay->addWidget(mid, 1);
    lay->addWidget(m_allow);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copyBtn, &QPushButton::clicked, this, &FormatDialog::copyDiskdef);
    connect(helpBtn, &QPushButton::clicked, this, [this] { showScoringHelp(this); });
    connect(tryBtn, &QPushButton::clicked, this, &FormatDialog::tryCustom);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &FormatDialog::selectionChanged);

    for (const Detect::Candidate &c : det.ranked) addCandidate(c, false);
    if (!m_cands.isEmpty()) m_list->setCurrentItem(m_list->topLevelItem(qBound(0, currentIndex, m_cands.size() - 1)));
    else m_details->setPlainText("No reading of this disc gives a CP/M directory. Try a format by hand on the right.");
}

void FormatDialog::addCandidate(const Detect::Candidate &c, bool select) {
    m_cands.append(c);
    auto *it = new QTreeWidgetItem;
    it->setText(0, QString::number(c.score, 'f', 1));
    it->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
    it->setText(1, c.fmt.name + (c.remembered ? " (chosen)" : ""));
    it->setToolTip(1, c.fmt.displayName());
    it->setText(2, QString::number(c.files));
    it->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    it->setText(3, c.fmt.shortSummary());
    it->setToolTip(3, c.fmt.summary());
    it->setData(0, Qt::UserRole, m_cands.size() - 1);
    m_list->addTopLevelItem(it);
    if (select) m_list->setCurrentItem(it);
}

Detect::Candidate FormatDialog::chosen() const {
    QTreeWidgetItem *it = m_list->currentItem();
    return it ? m_cands[it->data(0, Qt::UserRole).toInt()] : Detect::Candidate();
}

bool FormatDialog::allowChanges() const { return m_allow->isChecked(); }

void FormatDialog::selectionChanged() {
    QTreeWidgetItem *it = m_list->currentItem();
    if (!it) return;
    const Detect::Candidate &c = m_cands[it->data(0, Qt::UserRole).toInt()];
    if (!m_keepForm) fillForm(c.fmt);
    QStringList out;
    out << c.fmt.displayName() << c.fmt.summary();
    if (!c.fmt.source.isEmpty()) out << "Source: " + c.fmt.source;
    out << "" << "Evidence:";
    for (const QString &e : c.evidence) if (!e.startsWith("signature:")) out << "  " + e;
    out << "" << "Files this reading lists:";
    QString err;
    if (m_img->kind() == Disk::Kind::Raw && c.raw.valid()) m_img->setRawLayout(c.raw, &err);
    Cpm::Volume v;
    if (!v.attach(m_img, c.fmt, &err)) out << "  (can't read: " + err + ")";
    else {
        int n = 0;
        for (const Cpm::File &f : v.files()) {
            if (++n > 200) { out << QString("  ... and %1 more").arg(v.files().size() - 200); break; }
            out << QString("  %1:%2 %3 %4%5").arg(f.user, 2).arg(f.name, -12).arg(f.size, 8).arg(Cpm::attrString(f.attrs))
                       .arg(f.problem.isEmpty() ? QString() : "  [" + f.problem + "]");
        }
        if (v.files().isEmpty()) out << "  (none)";
        Cpm::Report r = v.check();
        out << "" << QString("Check: %1 errors, %2 warnings").arg(r.errors.size()).arg(r.warnings.size());
        for (const QString &e : r.errors.mid(0, 20)) out << "  error: " + e;
        for (const QString &e : r.warnings.mid(0, 10)) out << "  warning: " + e;
    }
    m_details->setPlainText(out.join('\n'));
}

void FormatDialog::fillForm(const Cpm::Format &f) {
    m_seclen->setCurrentText(QString::number(f.seclen));
    m_sectrk->setValue(f.sectrk);
    m_tracks->setValue(f.tracks);
    m_heads->setValue(f.physHeads);
    if (f.physHeads == 2) m_sides->setCurrentIndex(int(f.sides));    // one side has no order: keep the one shown
    m_boot->setValue(f.bootsec >= 0 ? 0 : f.boottrk);
    m_skew->setValue(f.skewtab.isEmpty() ? f.skew : 0);
    QStringList st;
    for (int v : f.skewtab) st << QString::number(v);
    m_skewtab->setText(st.join(','));
    m_bls->setCurrentText(QString::number(f.blocksize));
    m_maxdir->setValue(f.maxdir);
    m_dirblks->setValue(f.dirblks);
    m_os->setCurrentIndex(int(f.os));
    m_ptr16->setChecked(f.ptr16Forced);
    m_formErr->clear();
}

bool FormatDialog::formFormat(Cpm::Format *f, QString *err) const {
    Cpm::Format x;
    x.name = "by-hand";
    x.origin = "detected";
    x.seclen = m_seclen->currentText().toInt();
    x.sectrk = m_sectrk->value();
    x.tracks = m_tracks->value();
    x.heads = m_heads->value();
    x.sides = Cpm::SideOrder(m_sides->currentIndex());
    x.boottrk = m_boot->value();
    x.skew = m_skew->value();
    for (const QString &p : m_skewtab->text().split(',', Qt::SkipEmptyParts)) {
        bool ok;
        int v = p.trimmed().toInt(&ok);
        if (!ok) { *err = "the skew table must be numbers separated by commas"; return false; }
        x.skewtab << v;
    }
    x.blocksize = m_bls->currentText().toInt();
    x.maxdir = m_maxdir->value();
    x.dirblks = m_dirblks->value();
    x.os = Cpm::Os(m_os->currentIndex());
    x.ptr16Forced = m_ptr16->isChecked();
    if (!x.derive(err)) return false;
    *f = x;
    return true;
}

bool FormatDialog::evaluateFormat(const Cpm::Format &f, Detect::Candidate *out, QString *err) {
    if (m_img->kind() == Disk::Kind::Raw) {
        Disk::RawLayout l;
        l.seclen = f.seclen; l.sectrk = f.sectrk; l.heads = f.physHeads; l.cyls = f.cylinders;
        if (l.bytes() < m_img->rawFileSize() - qint64(f.sectrk) * f.seclen * f.physHeads)
            *err = QString("note: this geometry covers %1 bytes of the %2-byte image").arg(l.bytes()).arg(m_img->rawFileSize());
        QString e;
        if (!m_img->setRawLayout(l, &e)) { *err = e; return false; }
    } else {
        Disk::Shape sh = m_img->shape();
        if (f.seclen != sh.seclen || f.sectrk != sh.sectrk) {
            *err = QString("this disc's tracks hold %1 x %2-byte sectors").arg(sh.sectrk).arg(sh.seclen);
            return false;
        }
        if (f.physHeads > m_img->heads()) { *err = "this disc has only one side"; return false; }
    }
    *out = Detect::evaluate(*m_img, f, true);
    if (out->score < -1e8) { *err = out->evidence.join("; "); return false; }
    return true;
}

bool FormatDialog::tryDiskdef(const QString &text, QString *err) {
    QStringList w;
    QVector<Cpm::Format> l = Cpm::parseDiskdefs(text, "by hand", &w);
    if (l.isEmpty()) { *err = w.join("; "); return false; }
    Detect::Candidate c;
    if (!evaluateFormat(l.first(), &c, err)) return false;
    c.evidence << "chosen by hand";
    addCandidate(c, true);
    return true;
}

void FormatDialog::tryCustom() {
    Cpm::Format f;
    QString err;
    if (!formFormat(&f, &err)) { m_formErr->setText("<font color=#b00>" + err.toHtmlEscaped() + "</font>"); return; }
    Detect::Candidate c;
    if (!evaluateFormat(f, &c, &err)) { m_formErr->setText("<font color=#b00>" + err.toHtmlEscaped() + "</font>"); return; }
    // The same layout as a known format: score it as detection scores that format, so the number in the list means
    // the same thing whether the reading was found or typed.
    for (const Cpm::Format &l : Cpm::library())
        if (l.sameLayout(f)) {
            Detect::Candidate k = Detect::evaluate(*m_img, l, true);
            if (k.score > -1e8) {
                Detect::scoreKnownFormat(&k, l, *m_img);
                c = k;
            }
            break;
        }
    c.evidence << "set by hand";
    // Select the new reading without refilling the form from it: the form keeps exactly what was typed, so the
    // next try starts from the same settings (deriving a format normalises some of them).
    m_keepForm = true;
    addCandidate(c, true);
    m_keepForm = false;
    m_formErr->setText(err);
}

void FormatDialog::copyDiskdef() {
    Detect::Candidate c = chosen();
    if (c.fmt.derived) QApplication::clipboard()->setText(c.fmt.toDiskdef() + "\n");
}
