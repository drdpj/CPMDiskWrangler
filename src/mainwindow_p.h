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
// mainwindow_p.h - helpers shared by the window, the drag code and the dialogs. The tree layout helpers are
// DiskWranglerCore's, the same as Acorn Disk Wrangler's, so the two programs look alike side by side.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFontMetrics>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QTreeWidget>
#include <QVector>
#include <initializer_list>
#include <diskwrangler/ui/treecolumns.h>
#include <diskwrangler/ui/uihelpers.h>
#include <diskwrangler/xfer/hostname.h>
#include <diskwrangler/xfer/mime.h>

// Tree item roles. Every row carries what it is; nothing is ever parsed back from the cell text.
enum Roles { RoleKind = Qt::UserRole, RoleUser, RoleName, RoleSide };
enum RowKind { RowRoot = 0, RowUser = 1, RowFile = 2 };

// A file (or a host folder) read out of a disc or the host, ready to be written to a CP/M disc - or a CP/M file
// ready to be written to the host. The attribute fields are filled from whatever the source knows.
struct XItem {
    QString name;               // the name in the source (CP/M "NAME.EXT", an Acorn or DOS name, a host file name)
    QString hostName;           // what to call it on the host when that differs from name (a CP/M name shown in a national set)
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
    bool cancelled = false, natAsked = false;
    QStringList errors, renamed;
};

// The status line: one line that never makes the window wider. It is given several versions of the text, longest first,
// shows the longest that fits, and cuts the last with "..." if even that doesn't; the longest is always in the tooltip.
class StatusLabel : public QLabel {
public:
    explicit StatusLabel(QWidget *parent = nullptr) : QLabel(parent) { setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); }
    void setText(const QString &t) { setVariants({t}); }
    void setVariants(const QStringList &v) { m_v = v; setToolTip(v.isEmpty() ? QString() : v.first()); update(); }
    QString shown() const {                       // the version that fits the width now
        for (const QString &t : m_v) if (fontMetrics().horizontalAdvance(t) <= width()) return t;
        return m_v.isEmpty() ? QString() : fontMetrics().elidedText(m_v.last(), Qt::ElideRight, width());
    }
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }
    QSize sizeHint() const override { return QSize(0, QLabel::sizeHint().height()); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.drawText(rect(), int(alignment()) | Qt::AlignVCenter, shown());
    }
private:
    QStringList m_v;
};

// "user3", "User 3", "u3" or "3": a host folder that stands for a CP/M user number (as extraction writes them).
inline int userFromFolderName(const QString &n) {
    QString s = n.trimmed().toLower();
    if (s.startsWith("user")) s = s.mid(4).trimmed();
    else if (s.startsWith('u')) s = s.mid(1);
    bool ok = false;
    int u = s.toInt(&ok);
    return ok && u >= 0 && u <= 31 && !s.isEmpty() && s.size() <= 2 ? u : -1;
}
