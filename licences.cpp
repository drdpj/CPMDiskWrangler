/*
 * Licence viewer - shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "licences.h"
#include <QApplication>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

static const char *const kRoot = ":/licences";

// Every text under :/licences, as paths relative to it: the program's own first (in the order licences.qrc
// lists them, which the numeric prefixes keep), then the bundled libraries' in name order.
static QStringList licenceFiles() {
    QStringList own, bundled;
    QDirIterator it(kRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString rel = it.next().mid(int(qstrlen(kRoot)) + 1);
        (rel.startsWith("Bundled/") ? bundled : own) << rel;
    }
    own.sort(Qt::CaseInsensitive);
    bundled.sort(Qt::CaseInsensitive);
    return own + bundled;
}

// "1 This program - GNU GPL v3" -> "This program - GNU GPL v3"; "Bundled/zlib/LICENSE" -> "zlib: LICENSE"
static QString displayName(QString rel) {
    if (rel.endsWith(".md")) rel.chop(3);          // a Markdown text is marked by its name (see licences.qrc), not shown with it
    if (rel.startsWith("Bundled/")) {
        QString r = rel.mid(8).trimmed();       // the index's name starts with a space, to sort first
        int slash = r.indexOf('/');
        return slash < 0 ? r : r.left(slash) + ": " + r.mid(slash + 1);
    }
    int sp = rel.indexOf(' ');
    return (sp > 0 && rel.at(0).isDigit()) ? rel.mid(sp + 1) : rel;
}

void showLicences(QWidget *parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QApplication::applicationDisplayName().isEmpty()
                           ? QString("Licences")
                           : QApplication::applicationDisplayName() + " - Licences");
    auto *list = new QListWidget;
    // A text whose name ends in .md (the third-party notices) is shown as the Markdown it is; the licences themselves
    // stay plain text in a fixed-width font. Links open in the system's browser.
    auto *text = new QTextBrowser;
    text->setOpenLinks(false);
    QObject::connect(text, &QTextBrowser::anchorClicked, text, [](const QUrl &u) {
        if (u.scheme() == "http" || u.scheme() == "https") QDesktopServices::openUrl(u);
    });

    const QStringList files = licenceFiles();
    for (const QString &rel : files) {
        auto *item = new QListWidgetItem(displayName(rel), list);
        item->setData(Qt::UserRole, QString(kRoot) + "/" + rel);
    }
    QObject::connect(list, &QListWidget::currentItemChanged, text, [text](QListWidgetItem *cur) {
        if (!cur) { text->clear(); return; }
        const QString path = cur->data(Qt::UserRole).toString();
        QFile f(path);
        const QString body = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        if (path.endsWith(".md")) {
            text->document()->setDefaultFont(QApplication::font());
            text->setLineWrapMode(QTextEdit::WidgetWidth);
            text->setMarkdown(body);
        } else {
            text->document()->setDefaultFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            text->setLineWrapMode(QTextEdit::NoWrap);
            text->setPlainText(body);
        }
    });

    auto *split = new QSplitter;
    split->addWidget(list);
    split->addWidget(text);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 640});

    bool hasBundled = !files.isEmpty() && files.last().startsWith("Bundled/");
    auto *note = new QLabel(hasBundled
        ? "This program, and the licences of the libraries distributed with it. Qt is used under the LGPL v3: "
          "its libraries are separate files that you may replace with your own builds."
        : "This program's licence and notices. (A release build also lists the libraries distributed with it.)");
    note->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(note);
    lay->addWidget(split, 1);
    lay->addWidget(buttons);
    dlg.resize(960, 600);
    if (list->count()) list->setCurrentRow(0);
    dlg.exec();
}
