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
#pragma once
#include <QByteArray>
#include <QDialog>

class QComboBox;
class QCheckBox;
class QLabel;
class QPlainTextEdit;

// Read-only window showing a file as CP/M text or a hex dump. Text is selectable (Ctrl+C), and there are
// Copy all / Save as text buttons.
class ViewerDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { Text = 0, Hex = 1 };
    ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent = nullptr);
    Mode mode() const;
    QString shownText() const;

private slots:
    void render();
    void copyAll();
    void saveText();

private:
    QString m_title;
    QByteArray m_data;
    QComboBox *m_mode;
    QCheckBox *m_wrap, *m_eof, *m_strip;
    QLabel *m_info;
    QPlainTextEdit *m_edit;
};
