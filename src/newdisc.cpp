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
#include "newdisc.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

NewDiscDialog::NewDiscDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("New disc image");
    resize(640, 560);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filter formats (name or description)");
    m_list = new QListWidget;
    for (const dw::fs::Preset &p : Cpm::libraryPresets()) {   // the library's floppy formats, from the presets registry
        auto *it = new QListWidgetItem(p.name);
        it->setData(Qt::UserRole, p.id);
        if (!p.notes.isEmpty()) { QFont b = it->font(); b.setBold(true); it->setFont(b); }
        m_list->addItem(it);
    }
    m_list->sortItems();
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_kind = new QComboBox;
    m_kind->addItem("ImageDisk (.IMD)", int(dw::device::Kind::Imd));
    m_kind->addItem("Extended DSK image (.DSK)", int(dw::device::Kind::Edsk));
    m_kind->setItemData(1, "The DSK format CPCEMU introduced, now used by emulators and tools for many machines", Qt::ToolTipRole);
    m_kind->addItem("Standard DSK image (.DSK)", int(dw::device::Kind::Dsk));
    m_kind->setItemData(2, "The original CPCEMU DSK header, which older emulators (ubee512, for one) read; every track must be the same "
                           "size, so formats with a different track 0 can't use it", Qt::ToolTipRole);
    m_kind->addItem("Raw sector image (.IMG)", int(dw::device::Kind::Raw));
    m_twoSided = new QCheckBox("Double-sided: a separate disc on each side");
    m_twoSided->setToolTip("Research Machines double-sided discs hold two separate CP/M discs, one on each side, each with its "
                           "own single-density boot track.\nTick this to make one: both sides are formatted and each is its own "
                           "drive in the window.\nLeft unticked you get a single-sided disc. Only formats that exist as "
                           "double-sided discs can be ticked.");
    m_stamps = new QCheckBox("CP/M 3 time stamps (a label and stamp entries in the directory)");
    m_label = new QLineEdit;
    m_label->setMaxLength(12);
    m_label->setPlaceholderText("label (CP/M 3), e.g. WORK.DSK");
    auto *fl = new QFormLayout;
    fl->addRow("Container", m_kind);
    fl->addRow(m_twoSided);
    fl->addRow(m_stamps);
    fl->addRow("Disc label", m_label);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Create...");
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel("Format (bold: common formats with a description; the rest come from cpmtools' diskdefs):"));
    lay->addWidget(m_filter);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_info);
    lay->addLayout(fl);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged, this, &NewDiscDialog::filter);
    connect(m_list, &QListWidget::currentRowChanged, this, &NewDiscDialog::changed);
    connect(m_stamps, &QCheckBox::toggled, m_label, &QWidget::setEnabled);
    connect(m_twoSided, &QCheckBox::toggled, this, &NewDiscDialog::changed);
    selectFormat("ibm-3740");
}

void NewDiscDialog::filter(const QString &text) {
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setHidden(!m_list->item(i)->text().contains(text, Qt::CaseInsensitive));
}

void NewDiscDialog::changed() {
    QListWidgetItem *it = m_list->currentItem();
    if (!it) return;
    const Cpm::Format *f = Cpm::findFormat(it->data(Qt::UserRole).toString());
    if (!f) return;
    const bool twoOk = f->twoSidedOk && f->physHeads == 1;
    if (!twoOk && m_twoSided->isChecked()) { QSignalBlocker b(m_twoSided); m_twoSided->setChecked(false); }
    m_twoSided->setEnabled(twoOk);
    QString s = format().summary();
    if (f->heads == 0 && f->tracks % 2 == 0 && f->tracks <= 86)
        s += QString("\nThis definition doesn't say how many sides the disc has; it will be made with %1.").arg(f->physHeads == 1 ? "one" : "two");
    if (!f->source.isEmpty()) s += "\nSource: " + f->source;
    s += "\nFrom: " + f->origin;
    m_info->setText(s);
    m_stamps->setChecked(f->os == Cpm::Os::V3);
    m_label->setEnabled(m_stamps->isChecked());
}

bool NewDiscDialog::selectFormat(const QString &name) {
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == name) { m_list->setCurrentRow(i); return true; }
    return false;
}

Cpm::Format NewDiscDialog::format() const {
    QListWidgetItem *it = m_list->currentItem();
    const Cpm::Format *f = it ? Cpm::findFormat(it->data(Qt::UserRole).toString()) : nullptr;
    if (!f) return Cpm::Format();
    if (m_twoSided->isChecked() && f->twoSidedOk && f->physHeads == 1) {
        Cpm::Format g;
        QString e;
        if (Cpm::separateSidesVariant(*f, &g, &e)) return g;
    }
    return *f;
}

dw::device::Kind NewDiscDialog::kind() const { return dw::device::Kind(m_kind->currentData().toInt()); }
bool NewDiscDialog::stamps() const { return m_stamps->isChecked(); }
QString NewDiscDialog::label() const { return m_label->text().trimmed().toUpper(); }
