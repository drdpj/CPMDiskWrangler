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
