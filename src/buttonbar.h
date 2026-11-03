/*
 * Button bar - shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The main window's row of buttons. A QToolBar's own buttons are drawn flat by most styles (on macOS just text),
// so nothing says they can be pressed. makeButtonBar() fills a tool bar with real QPushButtons instead, each kept
// in step with its QAction: text (the action's iconText()), enabled, visible, tool tip, icon, checkable and checked.
// The actions stay the single source of truth - menus, shortcuts and updateUi() work on them as before. A null
// action in the list adds a separator. Header only, so the tests that build mainwindow.cpp need nothing more.

#include <QAction>
#include <QLayout>
#include <QList>
#include <QPushButton>
#include <QToolBar>

namespace ButtonBar {

inline QPushButton *addButton(QToolBar *tb, QAction *a) {
    auto *b = new QPushButton(tb);
    b->setFocusPolicy(Qt::NoFocus);          // the file list keeps the keyboard focus (and so its selection)
    b->setAutoDefault(false);
    b->setObjectName(a->objectName().isEmpty() ? QString() : a->objectName() + "Button");
    QAction *slot = tb->addWidget(b);        // shows or hides the button on the bar
    auto sync = [a, b, slot] {
        b->setText(a->iconText());
        b->setIcon(a->icon());
        b->setToolTip(a->toolTip());
        b->setEnabled(a->isEnabled());
        b->setCheckable(a->isCheckable());
        b->setChecked(a->isChecked());
        slot->setVisible(a->isVisible());
    };
    sync();
    QObject::connect(a, &QAction::changed, b, sync);
    QObject::connect(b, &QPushButton::clicked, a, [a, sync] {
        a->trigger();
        sync();                              // a checkable action that refused to toggle puts the button back
    });
    return b;
}

inline void makeButtonBar(QToolBar *tb, const QList<QAction *> &actions) {
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setContextMenuPolicy(Qt::PreventContextMenu);   // nothing to hide or move
    tb->setContentsMargins(4, 2, 4, 2);
    if (tb->layout()) tb->layout()->setSpacing(4);
    for (QAction *a : actions) {
        if (a) addButton(tb, a);
        else tb->addSeparator();
    }
}

} // namespace ButtonBar
