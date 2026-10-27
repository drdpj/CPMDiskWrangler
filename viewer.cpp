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
#include "viewer.h"
#include "viewtext.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

static const int kMaxShown = 1024 * 1024;

ViewerDialog::ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent)
    : QDialog(parent), m_title(title), m_data(data) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("View - " + title);
    resize(860, 620);
    m_mode = new QComboBox;
    m_mode->addItems({"Text", "Hex dump"});
    m_wrap = new QCheckBox("Wrap lines");
    m_eof = new QCheckBox("Stop at ^Z");
    m_eof->setToolTip("CP/M text files end at the first ^Z (1A hex); the rest of the last record is padding");
    m_eof->setChecked(true);
    m_strip = new QCheckBox("Ignore bit 7 (WordStar)");
    m_strip->setChecked(ViewText::looksLikeWordStar(data));
    auto *copy = new QPushButton("Copy all");
    auto *save = new QPushButton("Save as text...");
    m_info = new QLabel;
    m_edit = new QPlainTextEdit;
    m_edit->setReadOnly(true);
    m_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("View as:"));
    top->addWidget(m_mode);
    top->addWidget(m_eof);
    top->addWidget(m_strip);
    top->addWidget(m_wrap);
    top->addStretch();
    top->addWidget(copy);
    top->addWidget(save);
    auto *lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addWidget(m_edit, 1);
    lay->addWidget(m_info);
    m_mode->setCurrentIndex(ViewText::looksLikeText(data) ? Text : Hex);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ViewerDialog::render);
    connect(m_eof, &QCheckBox::toggled, this, &ViewerDialog::render);
    connect(m_strip, &QCheckBox::toggled, this, &ViewerDialog::render);
    connect(m_wrap, &QCheckBox::toggled, this, [this](bool on) {
        m_edit->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });
    connect(copy, &QPushButton::clicked, this, &ViewerDialog::copyAll);
    connect(save, &QPushButton::clicked, this, &ViewerDialog::saveText);
    render();
}

ViewerDialog::Mode ViewerDialog::mode() const { return Mode(m_mode->currentIndex()); }
QString ViewerDialog::shownText() const { return m_edit->toPlainText(); }

void ViewerDialog::render() {
    QString note;
    bool truncated = m_data.size() > kMaxShown;
    QByteArray d = truncated ? m_data.left(kMaxShown) : m_data;
    m_eof->setEnabled(mode() == Text);
    m_strip->setEnabled(mode() == Text);
    if (mode() == Text) {
        int eof = -1;
        m_edit->setPlainText(ViewText::cpmText(d, m_eof->isChecked(), m_strip->isChecked(), &eof));
        if (eof >= 0) note = QString("^Z (end of text) at byte %1").arg(eof);
    } else {
        m_edit->setPlainText(ViewText::hexDump(d));
    }
    if (truncated) note += (note.isEmpty() ? "" : " - ") + QString("showing the first 1 MiB only");
    m_info->setText(QString("%1 bytes%2").arg(m_data.size()).arg(note.isEmpty() ? QString() : "  -  " + note));
}

void ViewerDialog::copyAll() { QApplication::clipboard()->setText(m_edit->toPlainText()); }

void ViewerDialog::saveText() {
    QString p = QFileDialog::getSaveFileName(this, "Save as text", m_title + (mode() == Hex ? ".hex.txt" : ".txt"));
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(m_edit->toPlainText().toUtf8()) < 0)
        QMessageBox::warning(this, "Save", f.errorString());
}
