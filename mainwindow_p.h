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
// mainwindow_p.h - helpers shared by the window, the drag code and the dialogs. The tree layout helpers are the
// same as Acorn Disk Wrangler's, so the two programs look alike side by side.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFontMetrics>
#include <QHeaderView>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QTreeWidget>
#include <QVector>
#include <initializer_list>

// Tree item roles. Every row carries what it is; nothing is ever parsed back from the cell text.
enum Roles { RoleKind = Qt::UserRole, RoleUser, RoleName };
enum RowKind { RowRoot = 0, RowUser = 1, RowFile = 2 };

#define CPMDW_MIME_ITEMS  "application/x-cpmdiskwrangler-items"
#define CPMDW_MIME_SOURCE "application/x-cpmdiskwrangler-source"

// A file (or a host folder) read out of a disc or the host, ready to be written to a CP/M disc - or a CP/M file
// ready to be written to the host. The attribute fields are filled from whatever the source knows.
struct XItem {
    QString name;               // the name in the source (CP/M "NAME.EXT", an Acorn or DOS name, a host file name)
    QString srcFs;              // "cpm", "adfs", "dfs", "fat", or empty for a host file
    bool isDir = false;
    QByteArray data;
    int user = -1;              // CP/M user number, -1 if the source has none
    bool readOnly = false, system = false, archive = false;
    quint8 interfaceAttrs = 0;  // F1'-F4' as bits 0-3 (CP/M sources)
    QDateTime when;             // the source's date (invalid if none); local, no zone
    QVector<XItem> kids;
};

struct ImportCtx {
    int files = 0, replaced = 0, skipped = 0, flattened = 0;
    int policy = 0;             // 0 = ask, 1 = replace all, 2 = skip all
    bool cancelled = false;
    QStringList errors, renamed;
};

struct ColSpec {
    const char *label;
    const char *sample;         // widest text the column normally shows (ignored for the Name column)
    bool numeric;
};

static const char *const NumPad = "  ";

inline void setupTreeColumns(QTreeWidget *t, std::initializer_list<ColSpec> cols) {
    const QFontMetrics fm(t->font());
    const int margin = 2 * t->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, t) + fm.averageCharWidth();
    QStringList labels;
    for (const ColSpec &c : cols) labels << (c.numeric ? QString(c.label) + NumPad : QString(c.label));
    t->setColumnCount(labels.size());
    t->setHeaderLabels(labels);
    QHeaderView *h = t->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    int fixed = 0, i = 0;
    for (const ColSpec &c : cols) {
        if (i > 0) {
            QString pad = c.numeric ? NumPad : "";
            int w = qMax(fm.horizontalAdvance(c.sample + pad), fm.horizontalAdvance(labels[i])) + margin;
            t->setColumnWidth(i, w);
            fixed += w;
            if (c.numeric) t->headerItem()->setTextAlignment(i, Qt::AlignRight | Qt::AlignVCenter);
        }
        ++i;
    }
    int sb = t->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, t);
    t->setMinimumWidth(fixed + fm.averageCharWidth() * 22 + sb + 2 * t->frameWidth());
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->setUniformRowHeights(true);
    t->setContextMenuPolicy(Qt::CustomContextMenu);
}

inline void setNumCell(QTreeWidgetItem *it, int col, const QString &text) {
    it->setText(col, text + NumPad);
    it->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
}

inline QString rootLabel(const QString &base, const QString &title) {
    return title.isEmpty() ? base : base + "   [" + title + "]";
}

inline QString hostSafeName(const QString &n) {
    QString s = n;
    for (QChar &c : s)
        if (QString("/\\:*?\"<>|").contains(c) || c.unicode() < 0x20) c = '_';
    static const QStringList dev = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (dev.contains(s.section('.', 0, 0), Qt::CaseInsensitive)) s.insert(s.indexOf('.') < 0 ? s.size() : s.indexOf('.'), '_');
    if (s.isEmpty() || s == "." || s == "..") s = "_" + s;
    while (s.endsWith('.') || s.endsWith(' ')) s[s.size() - 1] = '_';
    return s;
}

inline QString fmtBytes(qint64 b) {
    if (b >= 1024LL * 1024) return QString("%1 MB").arg(double(b) / (1024.0 * 1024.0), 0, 'f', 1);
    if (b >= 1024) return QString("%1 KB").arg(double(b) / 1024.0, 0, 'f', 1);
    return QString("%1 bytes").arg(b);
}

// "user3", "User 3", "u3" or "3": a host folder that stands for a CP/M user number (as extraction writes them).
inline int userFromFolderName(const QString &n) {
    QString s = n.trimmed().toLower();
    if (s.startsWith("user")) s = s.mid(4).trimmed();
    else if (s.startsWith('u')) s = s.mid(1);
    bool ok = false;
    int u = s.toInt(&ok);
    return ok && u >= 0 && u <= 31 && !s.isEmpty() && s.size() <= 2 ? u : -1;
}
