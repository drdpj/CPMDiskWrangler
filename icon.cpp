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
#include "icon.h"
#include <QPainter>
#include <QPainterPath>

// Laid out on a 100 x 100 grid. An 8-inch disc: a square sleeve with a large hub opening, a long oval head slot
// below it, the index hole beside the hub, a write-enable notch on the lower right edge and a label at the top.
QImage floppyIconImage(int size) {
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 100.0, size / 100.0);
    p.setPen(Qt::NoPen);
    const QColor liner("#5b5146"), dark("#1d1a17");

    QPainterPath sleeve;
    sleeve.addRoundedRect(QRectF(5, 5, 90, 90), 2.5, 2.5);
    QPainterPath notch;
    notch.addRect(QRectF(92, 74, 3.2, 6));
    p.setBrush(QColor("#3a332d"));                       // warm dark brown-black, like many 8" sleeves
    p.drawPath(sleeve.subtracted(notch));
    p.setBrush(QColor("#4a4239"));
    p.drawRoundedRect(QRectF(7, 6.3, 86, 2.2), 1.1, 1.1);

    // label: white with a green band (the CP/M colour of many distribution discs) and ruled lines
    p.setBrush(QColor("#f4f1e8"));
    p.drawRoundedRect(QRectF(12, 11, 52, 17), 1.6, 1.6);
    p.setBrush(QColor("#2f8a4c"));
    p.drawRect(QRectF(12, 11, 52, 5));
    p.setBrush(QColor("#8c8779"));
    p.drawRect(QRectF(16, 19.5, 38, 1.4));
    p.drawRect(QRectF(16, 23.5, 28, 1.4));

    // hub opening: large on an 8" disc
    const QPointF c(50, 50);
    p.setBrush(liner);
    p.drawEllipse(c, 15.5, 15.5);
    p.setBrush(QColor("#6d6156"));                       // the disc surface showing through
    p.drawEllipse(c, 13.0, 13.0);
    p.setBrush(dark);
    p.drawEllipse(c, 8.4, 8.4);
    p.setBrush(liner);                                   // index hole
    p.drawEllipse(QPointF(c.x() + 21.5, c.y() - 6.5), 2.1, 2.1);

    // head slot: a long oval reaching towards the bottom edge
    p.setBrush(liner);
    p.drawRoundedRect(QRectF(45.5, 71, 9, 21), 4.5, 4.5);
    return img;
}

QIcon appIcon() {
    QIcon ic;
    const int sizes[] = {16, 20, 24, 32, 48, 64, 128, 256};
    for (int s : sizes) ic.addPixmap(QPixmap::fromImage(floppyIconImage(s)));
    return ic;
}
