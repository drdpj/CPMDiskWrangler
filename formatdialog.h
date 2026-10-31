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
// formatdialog.h - Tools > Disc format: what detection made of the disc, the other readings it considered (with
// the evidence for each and a preview of the files each one lists), and a form to try a format by hand.
// Nothing is written: choosing a format only changes how the image is read.
#pragma once
#include <QDialog>
#include "detect.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTreeWidget;

class FormatDialog : public QDialog {
    Q_OBJECT
public:
    // img: the open image (a raw image's layout may be changed while previewing; the caller sets it back).
    FormatDialog(Disk::Image *img, const Detect::Result &det, int currentIndex, bool writable, QWidget *parent = nullptr);
    Detect::Candidate chosen() const;
    bool allowChanges() const;
    // for tests: try a format given in diskdefs syntax and select it
    bool tryDiskdef(const QString &text, QString *err);

private slots:
    void selectionChanged();
    void tryCustom();
    void copyDiskdef();

private:
    void addCandidate(const Detect::Candidate &c, bool select);
    void fillForm(const Cpm::Format &f);
    bool formFormat(Cpm::Format *f, QString *err) const;
    bool evaluateFormat(const Cpm::Format &f, Detect::Candidate *out, QString *err);

    Disk::Image *m_img;
    bool m_keepForm = false;         // selecting a reading just tried by hand: leave the form as typed
    QVector<Detect::Candidate> m_cands;
    QTreeWidget *m_list;
    QPlainTextEdit *m_details;
    QCheckBox *m_allow, *m_ptr16;
    QLabel *m_formErr;
    QComboBox *m_seclen, *m_sides, *m_bls, *m_os;
    QSpinBox *m_sectrk, *m_tracks, *m_heads, *m_boot, *m_skew, *m_maxdir, *m_dirblks;
    QLineEdit *m_skewtab;
};
