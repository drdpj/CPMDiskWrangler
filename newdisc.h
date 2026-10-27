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
// newdisc.h - File > New disc image: pick a format from the library, the container, CP/M 3 time stamps and label.
#pragma once
#include <QDialog>
#include "cpmformat.h"
#include "diskimage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

class NewDiscDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewDiscDialog(QWidget *parent = nullptr);
    Cpm::Format format() const;
    Disk::Kind kind() const;
    bool stamps() const;
    QString label() const;
    bool selectFormat(const QString &name);           // for tests
    void setKind(Disk::Kind k);
private slots:
    void filter(const QString &text);
    void changed();
private:
    QLineEdit *m_filter, *m_label;
    QListWidget *m_list;
    QLabel *m_info;
    QComboBox *m_kind;
    QCheckBox *m_stamps;
};
