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
#include "mainwindow.h"
#include "appsettings.h"

#include "buttonbar.h"
#include "licences.h"
#include "formatdialog.h"
#include "newdisc.h"
#include "viewer.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

bool MainWindow::autoAnswer = false;

// ================================================================================================ DropTree

static QByteArray sourceTag(quint64 owner) {
    return QByteArray::number(qint64(QCoreApplication::applicationPid())) + ":" + QByteArray::number(owner);
}

DropTree::DropTree(QWidget *parent) : QTreeWidget(parent) {
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(true);
}

QStringList DropTree::mimeTypes() const { return {CPMDW_MIME_ITEMS, RetroFiles::MimeType, "text/uri-list"}; }
Qt::DropActions DropTree::supportedDropActions() const { return Qt::CopyAction; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QMimeData *DropTree::mimeData(const QList<QTreeWidgetItem *> &items) const { return makeMime(items); }
#else
QMimeData *DropTree::mimeData(const QList<QTreeWidgetItem *> items) const { return makeMime(items); }
#endif

// Where a drag is over the tree: QDropEvent::pos() is deprecated in Qt 6 and position() is new in it.
static QPoint dropPos(const QDropEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position().toPoint();
#else
    return e->pos();
#endif
}

bool DropTree::fromThisProcess(const QMimeData *m) {
    if (!m->hasFormat(CPMDW_MIME_SOURCE) || !m->hasFormat(CPMDW_MIME_ITEMS)) return false;
    QList<QByteArray> f = m->data(CPMDW_MIME_SOURCE).split(':');
    return f.size() == 2 && f[0].toLongLong() == qint64(QCoreApplication::applicationPid()) &&
           MainWindow::byId(f[1].toULongLong()) != nullptr;
}

QMimeData *DropTree::makeMime(const QList<QTreeWidgetItem *> &items) const {
    MainWindow *win = MainWindow::byId(m_owner);
    if (!win) return nullptr;
    QVector<FileRef> refs;
    for (QTreeWidgetItem *it : items) {
        int kind = it->data(0, RoleKind).toInt();
        int side = it->data(0, RoleSide).toInt();
        if (kind == RowFile) refs << FileRef(side, it->data(0, RoleUser).toInt(), it->data(0, RoleName).toString());
        else                    // a user group or a side's root: every file in it
            for (const Cpm::File &f : win->volume(side).files())
                if (kind == RowRoot || f.user == it->data(0, RoleUser).toInt()) refs << FileRef(side, f.user, f.name);
    }
    QVector<FileRef> unique;
    for (const FileRef &r : refs) if (!unique.contains(r)) unique << r;
    if (unique.isEmpty()) return nullptr;
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << quint64(m_owner) << qint32(unique.size());
    for (const FileRef &r : unique) ds << qint32(r.side) << qint32(r.user) << r.name;
    QPointer<MainWindow> pw = win;
    RetroFiles::DragOutMime::Exporter exporter = [pw, unique](QVector<XItem> *out, QString *err) {
        if (!pw) { *err = "the disc's window was closed during the drag"; return false; }
        return pw->exportFiles(unique, out, err);
    };
    auto *m = new RetroFiles::DragOutMime(exporter, win->trimTextOnExtract(), QFileInfo(win->image().path()).fileName());
    m->setData(CPMDW_MIME_ITEMS, payload);
    m->setData(CPMDW_MIME_SOURCE, sourceTag(m_owner));
    m_lastDrag = m->state();
    return m;
}

void DropTree::startDrag(Qt::DropActions) {
    QMimeData *data = makeMime(selectedItems());
    if (!data) return;
    std::shared_ptr<RetroFiles::DragState> state = m_lastDrag;
    auto *drag = new QDrag(this);
    drag->setMimeData(data);
    if (QTreeWidgetItem *first = currentItem()) {
        QRect r = visualItemRect(first).intersected(viewport()->rect());
        if (r.isValid()) {
            r.setWidth(qMin(r.width(), columnWidth(0)));
            drag->setPixmap(viewport()->grab(r));
            drag->setHotSpot(viewport()->mapFromGlobal(QCursor::pos()) - r.topLeft());
        }
    }
    Qt::DropAction result = drag->exec(Qt::CopyAction, Qt::CopyAction);     // copy only: never "move" off the disc
    if (state && state->extracted && result == Qt::CopyAction) emit dragOutFinished(state->files, state->errors);
    m_lastDrag.reset();
}

bool DropTree::acceptable(const QMimeData *m) const {
    MainWindow *win = MainWindow::byId(m_owner);
    if (win && win->dropOpensImage()) return m->hasUrls();     // an empty window: a dropped file is opened
    if (!win || !win->writable()) return false;
    if (fromThisProcess(m)) {
        if (m->data(CPMDW_MIME_SOURCE) != sourceTag(m_owner)) return true;
        MainWindow *w = MainWindow::byId(m_owner);       // within one window: only between its two sides
        return w && w->sides() > 1;
    }
    return m->hasFormat(RetroFiles::MimeType) || m->hasUrls();
}

void DropTree::dragEnterEvent(QDragEnterEvent *e) {
    if (acceptable(e->mimeData())) e->acceptProposedAction();
}

void DropTree::dragMoveEvent(QDragMoveEvent *e) {
    if (!acceptable(e->mimeData())) { e->ignore(); return; }
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

void DropTree::dropEvent(QDropEvent *e) {
    if (!acceptable(e->mimeData())) return;
    if (fromThisProcess(e->mimeData())) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
        emit internalDrop(e->mimeData()->data(CPMDW_MIME_ITEMS), itemAt(dropPos(e)));
        return;
    }
    if (e->mimeData()->hasFormat(RetroFiles::MimeType)) {
        QByteArray json = e->mimeData()->data(RetroFiles::MimeType);
        if (!json.isEmpty()) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            emit retroFilesDropped(json, itemAt(dropPos(e)));
            return;
        }
    }
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (paths.isEmpty()) return;
    e->acceptProposedAction();
    emit pathsDropped(paths, itemAt(dropPos(e)));
}

// ================================================================================================ the window

QList<MainWindow *> &MainWindow::windows() { static QList<MainWindow *> w; return w; }

MainWindow *MainWindow::byId(quint64 id) {
    for (MainWindow *w : windows()) if (w->id() == id) return w;
    return nullptr;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    windows().append(this);
    m_tree = new DropTree;
    m_tree->setOwner(id());
    setupTreeColumns(m_tree, {{"Name", "", false}, {"Attr", "R/O SYS ARC", false}, {"Size", "1048576", true},
                              {"Blocks", "00000", true}, {"Date", "2026-12-31 23:59", false}});
    setCentralWidget(m_tree);
    m_status = new QLabel;
    statusBar()->addWidget(m_status, 1);
    resize(860, 600);

    // The menus, the context menu and the button bar keep the same order as Acorn Disk Wrangler's (which has a few
    // more items: Add folder, New directory, Boot option, Side order).
    QMenu *file = menuBar()->addMenu("&File");
    QAction *aNewWin = file->addAction("New &window", [] { auto *w = new MainWindow; w->setAttribute(Qt::WA_DeleteOnClose); w->show(); });
    aNewWin->setShortcut(QKeySequence("Ctrl+Shift+N"));
    QAction *aNew = file->addAction("&New disc image...", this, &MainWindow::newImageDialog);
    aNew->setShortcut(QKeySequence::New);
    QAction *aOpen = file->addAction("&Open...", this, &MainWindow::openDialog);
    aOpen->setShortcut(QKeySequence::Open);
    file->addSeparator();
    m_actSave = file->addAction("&Save", this, &MainWindow::save);
    m_actSave->setShortcut(QKeySequence::Save);
    m_actSaveAs = file->addAction("Save &as (ImageDisk, DSK or raw)...", this, &MainWindow::saveAsDialog);
    m_actSaveAs->setShortcut(QKeySequence("Ctrl+Shift+S"));
    m_actRevert = file->addAction("&Revert", this, &MainWindow::revert);
    file->addSeparator();
    file->addAction("Create &local config file...", this, [this] { AppSettings::createPortableFileDialog(this); });
    file->addSeparator();
    file->addAction("&Close", this, &QWidget::close)->setShortcut(QKeySequence::Close);
    file->addAction("&Quit", qApp, &QApplication::closeAllWindows)->setShortcut(QKeySequence::Quit);

    QMenu *edit = menuBar()->addMenu("&Edit");
    m_actView = edit->addAction("&View (text / hex)", this, &MainWindow::viewSelected);
    m_actView->setShortcut(QKeySequence("F3"));
    m_actView->setIconText("View");
    m_actExtract = edit->addAction("E&xtract to folder...", this, &MainWindow::extractSelected);
    m_actExtract->setShortcut(QKeySequence("Ctrl+E"));
    m_actExtract->setIconText("Extract");
    edit->addSeparator();
    m_actAdd = edit->addAction("&Add files...", this, &MainWindow::addFilesDialog);
    m_actAdd->setShortcut(QKeySequence("Ctrl+Shift+F"));
    m_actAdd->setIconText("Add files");
    edit->addSeparator();
    m_actCopy = edit->addAction("&Copy", this, &MainWindow::copySelected);
    m_actCopy->setShortcuts(QKeySequence::Copy);       // the platform's own keys (Ctrl+C, Ctrl+Ins / Cmd+C)
    m_actPaste = edit->addAction("&Paste", this, &MainWindow::paste);
    m_actPaste->setShortcuts(QKeySequence::Paste);     // (Ctrl+V, Shift+Ins / Cmd+V)
    edit->addSeparator();
    m_actRename = edit->addAction("Re&name...", this, &MainWindow::renameSelected);
    m_actRename->setShortcut(QKeySequence("F2"));
    m_actRename->setIconText("Rename");
    m_actProps = edit->addAction("P&roperties (attributes, user)...", this, &MainWindow::editProperties);
    m_actProps->setShortcut(QKeySequence("Alt+Return"));
    m_actProps->setIconText("Properties");
    m_actDelete = edit->addAction("&Delete", this, &MainWindow::deleteSelected);
    m_actDelete->setShortcuts(deleteKeys());
    m_actDelete->setIconText("Delete");
    edit->addSeparator();
    m_actLabel = edit->addAction("Disc &label...", this, &MainWindow::setLabelDialog);

    QMenu *tools = menuBar()->addMenu("&Tools");
    m_actCheck = tools->addAction("&Check disc", this, &MainWindow::checkDisc);
    m_actCheck->setShortcut(QKeySequence("Ctrl+K"));
    m_actFormat = tools->addAction("Disc &format...", this, &MainWindow::formatDialog);
    m_actFormat->setShortcut(QKeySequence("Ctrl+F"));
    m_actFormat->setIconText("Format");
    m_actInfo = tools->addAction("Disc &information", this, &MainWindow::discInfo);
    m_actInfo->setShortcut(QKeySequence("Ctrl+I"));
    tools->addAction("Add a &diskdefs file to the format library...", this, &MainWindow::addDiskdefsDialog);
    tools->addSeparator();
    AppSettings st;
    m_optTrim = tools->addAction("Trim the ^Z padding from text files when extracting");
    m_optTrim->setCheckable(true);
    m_optTrim->setChecked(st.value("trimText", false).toBool());
    m_optFolders = tools->addAction("Extract each user number into its own folder (userN)");
    m_optFolders->setCheckable(true);
    m_optFolders->setChecked(st.value("folderPerUser", true).toBool());
    m_optBackup = tools->addAction("Keep a .bak copy when saving");
    m_optBackup->setCheckable(true);
    m_optBackup->setChecked(st.value("backup", true).toBool());
    connect(m_optTrim, &QAction::toggled, [](bool on) { AppSettings().setValue("trimText", on); });
    connect(m_optFolders, &QAction::toggled, [](bool on) { AppSettings().setValue("folderPerUser", on); });
    connect(m_optBackup, &QAction::toggled, [](bool on) { AppSettings().setValue("backup", on); });

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("&About CP/M Disk Wrangler...", this, &MainWindow::about);
    help->addAction("About &Qt...", qApp, &QApplication::aboutQt);
    help->addAction("&Licences...", this, [this] { showLicences(this); });

    // Open / Save live in the File menu only; the button bar is for working on the disc contents.
    ButtonBar::makeButtonBar(addToolBar("Main"), {m_actView, m_actExtract, m_actAdd, m_actRename, m_actProps, m_actDelete,
                                                  nullptr, m_actFormat});
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::updateUi);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateUi);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        if (it && it->data(0, RoleKind).toInt() == RowFile) viewSelected();
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_tree, &DropTree::pathsDropped, this, &MainWindow::onPathsDropped);
    connect(m_tree, &DropTree::internalDrop, this, &MainWindow::onInternalDrop);
    connect(m_tree, &DropTree::retroFilesDropped, this, &MainWindow::onRetroFilesDropped);
    connect(m_tree, &DropTree::dragOutFinished, this, [this](int files, const QStringList &errors) {
        if (!errors.isEmpty()) message("Drag", errors.join("\n"));
        else m_status->setText(QString("Copied %1 file%2 out of the disc.").arg(files).arg(files == 1 ? "" : "s"));
    });
    refresh();
}

MainWindow::~MainWindow() { windows().removeAll(this); }

QString MainWindow::title() const {
    QString n = m_img.isOpen() ? (m_img.path().isEmpty() ? "untitled" : QFileInfo(m_img.path()).fileName()) : "no disc";
    return n + (m_img.hasPending() ? " *" : "") + " - CP/M Disk Wrangler";
}

void MainWindow::message(const QString &title, const QString &text, bool warning) {
    if (autoAnswer) { m_status->setText(title + ": " + text.section('\n', 0, 0)); return; }
    if (warning) QMessageBox::warning(this, title, text);
    else QMessageBox::information(this, title, text);
}

bool MainWindow::writable() const { return readOnlyReason().isEmpty(); }

QString MainWindow::readOnlyReason() const {
    if (!m_nvols) return "no disc";
    if (m_img.readOnlyContainer()) return "TeleDisk images are read-only here: use Save As to make an ImageDisk or DSK copy";
    if (!m_checkOk) return "the directory has errors (Tools > Check disc)";
    if (!m_safe) return "the format isn't confirmed (Tools > Disc format)";
    return QString();
}

bool MainWindow::trimTextOnExtract() const { return m_optTrim->isChecked(); }

int MainWindow::maxUser() const { return m_vols[0].format().os == Cpm::Os::V3 ? 15 : 31; }

void MainWindow::detachAll() {
    m_vols[0].detach();
    m_vols[1].detach();
    m_nvols = 0;
}

int MainWindow::currentSide() const {
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    return sel.isEmpty() ? 0 : qBound(0, sel.first()->data(0, RoleSide).toInt(), qMax(0, m_nvols - 1));
}

// ------------------------------------------------------------------------------------------- opening

QString MainWindow::rememberedFormat() const {
    AppSettings st;
    QString key = "formats/" + QCryptographicHash::hash(QFileInfo(m_img.path()).canonicalFilePath().toUtf8(),
                                                        QCryptographicHash::Sha1).toHex();
    QString v = st.value(key).toString();
    qint64 size = v.section('|', 0, 0).toLongLong();
    return size == QFileInfo(m_img.path()).size() ? v.section('|', 1) : QString();
}

void MainWindow::rememberFormat() {
    if (m_img.path().isEmpty() || !m_nvols) return;
    AppSettings st;
    QString key = "formats/" + QCryptographicHash::hash(QFileInfo(m_img.path()).canonicalFilePath().toUtf8(),
                                                        QCryptographicHash::Sha1).toHex();
    Cpm::Format f = m_vols[0].format();
    st.setValue(key, QString::number(QFileInfo(m_img.path()).size()) + "|" + f.toDiskdef());
}

bool MainWindow::attachFormat(const Detect::Candidate &c, QString *err) {
    if (m_img.kind() == Disk::Kind::Raw && c.raw.valid() && !m_img.setRawLayout(c.raw, err)) return false;
    detachAll();
    int n = c.fmt.separateSides ? 2 : 1;
    for (int side = 0; side < n; ++side) {
        Cpm::Format f = c.fmt;
        f.fixedHead = side;
        f.derived = false;
        if (!m_vols[side].attach(&m_img, f, err)) {
            if (side == 0) { detachAll(); return false; }
            *err = "side 1: " + *err;
            detachAll();
            return false;
        }
    }
    m_nvols = n;
    m_current = c;
    m_checkOk = true;
    for (int side = 0; side < n; ++side) m_checkOk = m_checkOk && m_vols[side].check().ok();
    return true;
}

bool MainWindow::openImage(const QString &path) {
    if (m_img.isOpen() && !confirmDiscard()) return false;
    QString err;
    Disk::Image img;
    if (!img.open(path, &err)) { message("Open", QString("%1:\n%2").arg(path, err)); return false; }
    detachAll();
    m_img = img;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_det = Detect::detect(m_img, rememberedFormat());
    QApplication::restoreOverrideCursor();
    m_fileTime = QFileInfo(path).lastModified();
    m_fileSize = QFileInfo(path).size();
    m_safe = m_det.safeToWrite;
    m_detIndex = 0;
    if (m_det.ranked.isEmpty() || !attachFormat(m_det.ranked.first(), &err)) {
        detachAll();
        refresh();
        if (!autoAnswer) {
            QMessageBox box(QMessageBox::Warning, "Open", QString("%1\n\n%2\n%3").arg(QFileInfo(path).fileName(), m_det.physical, m_det.why),
                            QMessageBox::Ok, this);
            QPushButton *fmt = box.addButton("Choose a format...", QMessageBox::ActionRole);
            box.exec();
            if (box.clickedButton() == fmt) formatDialog();
        }
        return false;
    }
    if (m_img.needsPadding()) m_img.markForSave();
    refresh();
    return true;
}

bool MainWindow::newImage(const QString &path, Disk::Kind kind, const Cpm::Format &f, bool stamps, const QString &label) {
    QString err;
    Disk::Image img;
    if (!Cpm::makeBlank(&img, kind, f, stamps, label, &err) || !img.saveAs(path, kind, &err)) {
        message("New disc image", err);
        return false;
    }
    detachAll();
    m_img = img;
    m_det = Detect::Result();
    Detect::Candidate c = Detect::evaluate(m_img, f, false);
    c.fmt = f;
    c.fmt.derive(&err);
    c.fromLibrary = true;
    c.raw = m_img.rawLayout();
    c.evidence << "a new disc made in this format";
    m_det.ranked << c;
    m_det.confidence = "high";
    m_det.why = "A new disc made here.";
    m_det.physical = QString("%1, new").arg(Disk::kindName(kind));
    m_detIndex = 0;
    m_safe = true;
    if (!attachFormat(c, &err)) { message("New disc image", err); return false; }
    m_fileTime = QFileInfo(path).lastModified();
    m_fileSize = QFileInfo(path).size();
    rememberFormat();
    refresh();
    return true;
}

bool MainWindow::useFormat(const Detect::Candidate &c, bool allowChanges) {
    if (m_img.hasPending()) { message("Disc format", "Save or revert your changes before reading the disc with another format."); return false; }
    QString err;
    if (!attachFormat(c, &err)) {
        message("Disc format", err);
        if (!m_det.ranked.isEmpty()) attachFormat(m_det.ranked.value(m_detIndex, m_det.ranked.first()), &err);
        refresh();
        return false;
    }
    m_detIndex = -1;
    for (int i = 0; i < m_det.ranked.size(); ++i) if (m_det.ranked[i].key() == c.key()) m_detIndex = i;
    m_safe = allowChanges;
    if (allowChanges) rememberFormat();
    refresh();
    return true;
}

bool MainWindow::confirmDiscard() {
    if (!m_img.hasPending()) return true;
    if (autoAnswer) return true;
    auto r = QMessageBox::question(this, "Unsaved changes", "Discard the changes to " + QFileInfo(m_img.path()).fileName() + "?",
                                   QMessageBox::Discard | QMessageBox::Cancel);
    return r == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (confirmDiscard()) e->accept(); else e->ignore();
}

void MainWindow::openDialog() {
    QString p = QFileDialog::getOpenFileName(this, "Open CP/M disc image", AppSettings().value("lastDir").toString(),
                                             "Disc images (*.imd *.dsk *.td0 *.img *.ima *.raw *.h8d *.ssd *.dsd *.bin);;All files (*)");
    if (p.isEmpty()) return;
    AppSettings().setValue("lastDir", QFileInfo(p).absolutePath());
    if (m_img.isOpen()) {
        auto *w = new MainWindow;
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
        w->openImage(p);
    } else {
        openImage(p);
    }
}

void MainWindow::newImageDialog() {
    NewDiscDialog d(this);
    if (d.exec() != QDialog::Accepted) return;
    Cpm::Format f = d.format();
    QString ext = Disk::kindExtension(d.kind());
    QString p = QFileDialog::getSaveFileName(this, "New disc image", AppSettings().value("lastDir").toString() + "/" + f.name + "." + ext,
                                             QString("Disc image (*.%1)").arg(ext));
    if (p.isEmpty()) return;
    MainWindow *w = this;
    if (m_img.isOpen()) {
        w = new MainWindow;
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    }
    w->newImage(p, d.kind(), f, d.stamps(), d.label());
}

void MainWindow::revert() {
    if (!m_img.isOpen() || m_img.path().isEmpty()) return;
    m_img.discardPending();
    openImage(m_img.path());
}

// ------------------------------------------------------------------------------------------- saving

bool MainWindow::save() {
    if (!m_img.isOpen() || !m_img.hasPending()) return true;
    if (m_img.readOnlyContainer()) { saveAsDialog(); return false; }
    if (!writable()) { message("Save", "The disc is read-only: " + readOnlyReason()); return false; }
    for (int side = 0; side < m_nvols; ++side) {
        Cpm::Report r = m_vols[side].check();
        if (!r.ok()) {
            message("Save", QString("Not saved: the directory%1 would not be consistent.\n\n").arg(m_nvols > 1 ? QString(" on side %1").arg(side) : QString()) +
                                r.errors.mid(0, 10).join("\n"));
            return false;
        }
    }
    QFileInfo fi(m_img.path());
    if (fi.exists() && (fi.lastModified() != m_fileTime || fi.size() != m_fileSize)) {
        message("Save", "Not saved: the image file has been changed by something else since it was opened. Use Save As, or Revert.");
        return false;
    }
    QString err;
    if (!m_img.commit(&err, m_optBackup->isChecked())) { message("Save", "Not saved: " + err); return false; }
    for (int side = 0; side < m_nvols; ++side) m_vols[side].reload(&err);
    m_fileTime = QFileInfo(m_img.path()).lastModified();
    m_fileSize = QFileInfo(m_img.path()).size();
    rememberFormat();
    refresh();
    m_status->setText("Saved " + QFileInfo(m_img.path()).fileName());
    return true;
}

bool MainWindow::saveAs(const QString &path, Disk::Kind kind) {
    if (!m_img.isOpen()) return false;
    bool ok = true;
    for (int side = 0; side < m_nvols; ++side) ok = ok && m_vols[side].check().ok();
    if (!ok && !m_img.readOnlyContainer()) {
        message("Save As", "Not saved: the directory has errors (Tools > Check disc).");
        return false;
    }
    QString err;
    if (!m_img.saveAs(path, kind, &err)) { message("Save As", "Not saved: " + err); return false; }
    m_current.raw = m_img.rawLayout();
    for (int side = 0; side < m_nvols; ++side)
        if (!m_vols[side].reload(&err)) { message("Save As", err); return false; }
    m_fileTime = QFileInfo(path).lastModified();
    m_fileSize = QFileInfo(path).size();
    if (writable() || m_safe) rememberFormat();
    refresh();
    return true;
}

void MainWindow::saveAsDialog() {
    if (!m_img.isOpen()) return;
    QString base = QFileInfo(m_img.path()).completeBaseName();
    QString filter;
    QString p = QFileDialog::getSaveFileName(this, "Save as", QFileInfo(m_img.path()).absolutePath() + "/" + base + ".imd",
                                             "ImageDisk (*.imd);;Extended CPCEMU DSK (*.dsk);;Raw sector image (*.img *.raw *.bin)", &filter);
    if (p.isEmpty()) return;
    Disk::Kind k = filter.startsWith("ImageDisk") ? Disk::Kind::Imd : filter.startsWith("Extended") ? Disk::Kind::Edsk : Disk::Kind::Raw;
    saveAs(p, k);
}

// ------------------------------------------------------------------------------------------- the tree

void MainWindow::refresh() {
    QSet<QString> sel, closed;
    for (QTreeWidgetItem *it : m_tree->selectedItems())
        sel << QString("%1:%2:%3:%4").arg(it->data(0, RoleSide).toInt()).arg(it->data(0, RoleKind).toInt())
                   .arg(it->data(0, RoleUser).toInt()).arg(it->data(0, RoleName).toString());
    for (int r = 0; r < m_tree->topLevelItemCount(); ++r) {
        QTreeWidgetItem *root = m_tree->topLevelItem(r);
        if (!root->isExpanded()) closed << QString("root%1").arg(r);
        for (int i = 0; i < root->childCount(); ++i)
            if (!root->child(i)->isExpanded()) closed << QString("%1:%2").arg(r).arg(root->child(i)->data(0, RoleUser).toInt());
    }
    m_tree->clear();
    for (int side = 0; side < m_nvols; ++side) {
        const Cpm::Volume &v = m_vols[side];
        auto *root = new QTreeWidgetItem(m_tree);
        QString title = v.label().present ? v.label().name : (m_nvols == 1 ? QFileInfo(m_img.path()).fileName() : QString());
        root->setText(0, rootLabel(m_nvols == 1 ? QString("A:") : QString("Side %1").arg(side), title));
        root->setData(0, RoleKind, RowRoot);
        root->setData(0, RoleUser, -1);
        root->setData(0, RoleSide, side);
        for (int u : v.usersInUse()) {
            auto *g = new QTreeWidgetItem(root);
            int n = 0;
            qint64 total = 0;
            for (const Cpm::File &f : v.files()) if (f.user == u) { ++n; total += f.size; }
            g->setText(0, QString("User %1   (%2 file%3)").arg(u).arg(n).arg(n == 1 ? "" : "s"));
            g->setData(0, RoleKind, RowUser);
            g->setData(0, RoleUser, u);
            g->setData(0, RoleSide, side);
            setNumCell(g, 2, QString::number(total));
            for (const Cpm::File &f : v.files()) {
                if (f.user != u) continue;
                auto *it = new QTreeWidgetItem(g);
                it->setText(0, f.name);
                it->setData(0, RoleKind, RowFile);
                it->setData(0, RoleUser, f.user);
                it->setData(0, RoleName, f.name);
                it->setData(0, RoleSide, side);
                it->setText(1, Cpm::attrString(f.attrs));
                setNumCell(it, 2, QString::number(f.size));
                setNumCell(it, 3, QString::number(f.blocks));
                QDateTime d = f.updated.isValid() ? f.updated : f.created;
                if (d.isValid()) it->setText(4, d.toString("yyyy-MM-dd HH:mm"));
                if (!f.problem.isEmpty()) { it->setToolTip(0, f.problem); it->setForeground(0, QColor("#b00000")); }
                if (sel.contains(QString("%1:%2:%3:%4").arg(side).arg(RowFile).arg(f.user).arg(f.name))) it->setSelected(true);
            }
            g->setExpanded(!closed.contains(QString("%1:%2").arg(side).arg(u)));
        }
        root->setExpanded(!closed.contains(QString("root%1").arg(side)));
    }
    updateUi();
}

void MainWindow::updateUi() {
    bool have = m_nvols > 0;
    bool w = writable();
    QVector<FileRef> refs = have ? selectedRefs() : QVector<FileRef>();
    bool oneFile = m_tree->selectedItems().size() == 1 && m_tree->selectedItems().first()->data(0, RoleKind).toInt() == RowFile;
    m_actSave->setEnabled(have && m_img.hasPending() && (w || m_img.readOnlyContainer()));
    m_actSaveAs->setEnabled(m_img.isOpen());
    m_actRevert->setEnabled(m_img.hasPending() && !m_img.path().isEmpty());
    m_actAdd->setEnabled(w);
    m_actDelete->setEnabled(w && !refs.isEmpty());
    m_actProps->setEnabled(have && !refs.isEmpty());
    m_actRename->setEnabled(w && oneFile);
    m_actLabel->setEnabled(w && m_vols[currentSide()].labelAllowed());
    m_actExtract->setEnabled(have && !refs.isEmpty());
    m_actCopy->setEnabled(have && !refs.isEmpty());
    m_actPaste->setEnabled(w && clipboardUsable());
    m_actView->setEnabled(have && oneFile);
    m_actCheck->setEnabled(have);
    m_actInfo->setEnabled(m_img.isOpen());
    m_actFormat->setEnabled(m_img.isOpen());
    setWindowTitle(title());
    if (!have) {
        m_status->setText(m_img.isOpen() ? "Not read as CP/M: " + m_det.why + " Drop another image here to open it."
                                         : "Open a disc image (File > Open), or drop one here.");
        return;
    }
    const Cpm::Format &f = m_vols[0].format();
    QString name = f.name == "detected" || f.name == "by-hand" ? QString("custom format") : f.name;
    QString s;
    if (m_nvols == 1) {
        s = QString("%1 - %2 file%3, %4 free of %5").arg(name).arg(m_vols[0].files().size())
                .arg(m_vols[0].files().size() == 1 ? "" : "s").arg(fmtBytes(m_vols[0].freeBytes())).arg(fmtBytes(f.capacityBytes()));
    } else {
        s = name + " (a disc on each side)";
        for (int side = 0; side < m_nvols; ++side)
            s += QString(" - side %1: %2 file%3, %4 free").arg(side).arg(m_vols[side].files().size())
                     .arg(m_vols[side].files().size() == 1 ? "" : "s").arg(fmtBytes(m_vols[side].freeBytes()));
    }
    if (!w) s += "  -  read-only: " + readOnlyReason();
    if (m_img.needsPadding()) s += "  -  the image file is short; it will be padded to full size when saved";
    m_status->setText(s);
}

QVector<FileRef> MainWindow::selectedRefs() const {
    QVector<FileRef> refs;
    for (QTreeWidgetItem *it : m_tree->selectedItems()) {
        int kind = it->data(0, RoleKind).toInt();
        int side = qBound(0, it->data(0, RoleSide).toInt(), qMax(0, m_nvols - 1));
        if (kind == RowFile) refs << FileRef(side, it->data(0, RoleUser).toInt(), it->data(0, RoleName).toString());
        else
            for (const Cpm::File &f : m_vols[side].files())
                if (kind == RowRoot || f.user == it->data(0, RoleUser).toInt()) refs << FileRef(side, f.user, f.name);
    }
    QVector<FileRef> unique;
    for (const FileRef &r : refs) if (!unique.contains(r)) unique << r;
    return unique;
}

int MainWindow::targetUser(QTreeWidgetItem *it) const {
    if (!it || it->data(0, RoleKind).toInt() == RowRoot) return -1;
    return it->data(0, RoleUser).toInt();
}

int MainWindow::targetSide(QTreeWidgetItem *it) const {
    return it ? qBound(0, it->data(0, RoleSide).toInt(), qMax(0, m_nvols - 1)) : 0;
}

void MainWindow::showContextMenu(const QPoint &pos) {
    QMenu m;                                    // the Edit menu's order, as Acorn Disk Wrangler's
    m.addActions({m_actView, m_actExtract});
    m.addSeparator();
    m.addAction(m_actAdd);
    m.addSeparator();
    m.addActions({m_actCopy, m_actPaste});
    m.addSeparator();
    m.addActions({m_actRename, m_actProps, m_actDelete});
    m.exec(m_tree->viewport()->mapToGlobal(pos));
}

// ------------------------------------------------------------------------------------------- copying out

bool MainWindow::exportFiles(const QVector<FileRef> &refs, QVector<XItem> *out, QString *err) const {
    out->clear();
    for (const FileRef &r : refs) {
        const Cpm::Volume &v = m_vols[qBound(0, r.side, 1)];
        int i = v.indexOf(r.user, r.name);
        if (i < 0) { *err = QString("%1:%2 is no longer on the disc").arg(r.user).arg(r.name); return false; }
        const Cpm::File &f = v.files()[i];
        XItem x;
        x.name = f.name;
        x.srcFs = "cpm";
        x.user = f.user;
        x.readOnly = f.attrs & Cpm::AttrRO;
        x.system = f.attrs & Cpm::AttrSYS;
        x.archive = f.attrs & Cpm::AttrARC;
        x.interfaceAttrs = quint8((f.attrs >> 4) & 0x0F);
        x.when = f.updated.isValid() ? f.updated : f.created;
        if (!v.readFile(f, &x.data, err)) return false;
        out->append(x);
    }
    return true;
}

void MainWindow::extractSelected() {
    QVector<FileRef> refs = selectedRefs();
    if (refs.isEmpty()) return;
    QString dir = QFileDialog::getExistingDirectory(this, "Extract to folder", AppSettings().value("extractDir").toString());
    if (dir.isEmpty()) return;
    AppSettings().setValue("extractDir", dir);
    QVector<XItem> items;
    QString err;
    if (!exportFiles(refs, &items, &err)) { message("Extract", err); return; }
    QSet<int> users;
    for (const XItem &x : items) users << x.user;
    QStringList top, errors;
    QJsonArray j;
    RetroFiles::writeHost(items, dir, m_optFolders->isChecked() && users.size() > 1, m_optTrim->isChecked(), &top, &j, &errors);
    if (!errors.isEmpty()) message("Extract", errors.join("\n"));
    m_status->setText(QString("Extracted %1 file%2 to %3").arg(items.size()).arg(items.size() == 1 ? "" : "s").arg(dir));
}

void MainWindow::viewSelected() {
    QVector<FileRef> refs = selectedRefs();
    if (refs.size() != 1) return;
    QVector<XItem> items;
    QString err;
    if (!exportFiles(refs, &items, &err)) { message("View", err); return; }
    QString t = QString("%1:%2").arg(refs[0].user).arg(refs[0].name);
    if (m_nvols > 1) t = QString("side %1 ").arg(refs[0].side) + t;
    auto *v = new ViewerDialog(t, items[0].data, this);
    v->show();
}

// ------------------------------------------------------------------------------------------- copying in

void MainWindow::onPathsDropped(const QStringList &paths, QTreeWidgetItem *target) {
    QStringList p = paths;
    if (dropOpensImage()) {                       // no disc here yet: open the first, the rest in new windows
        QTimer::singleShot(0, this, [this, p] {
            for (int i = 0; i < p.size(); ++i) {
                if (QFileInfo(p[i]).isDir()) continue;
                if (dropOpensImage()) { openImage(p[i]); continue; }
                auto *w = new MainWindow;
                w->setAttribute(Qt::WA_DeleteOnClose);
                w->show();
                w->openImage(p[i]);
            }
        });
        return;
    }
    int u = targetUser(target), side = targetSide(target);
    QTimer::singleShot(0, this, [this, p, u, side] { importPaths(p, u, side); });   // no dialogs inside the drop handler
}

void MainWindow::onRetroFilesDropped(const QByteArray &json, QTreeWidgetItem *target) {
    int u = targetUser(target), side = targetSide(target);
    QTimer::singleShot(0, this, [this, json, u, side] { importRetroFiles(json, u, side); });
}

void MainWindow::onInternalDrop(const QByteArray &payload, QTreeWidgetItem *target) {
    int u = targetUser(target), side = targetSide(target);
    QTimer::singleShot(0, this, [this, payload, u, side] {
        QDataStream ds(payload);
        quint64 src;
        qint32 n;
        ds >> src >> n;
        QVector<FileRef> refs;
        for (int i = 0; i < n; ++i) {
            qint32 s, user;
            QString name;
            ds >> s >> user >> name;
            if (src == id() && s == side) continue;        // within one window: only from one side to the other
            refs << FileRef(s, user, name);
        }
        MainWindow *w = byId(src);
        if (!w || refs.isEmpty()) return;
        QVector<XItem> items;
        QString err;
        if (!w->exportFiles(refs, &items, &err)) { message("Copy", err); return; }
        ImportCtx c;
        importItems(items, u, side, c);
        importSummary(c);
    });
}

bool MainWindow::importRetroFiles(const QByteArray &json, int user, int side) {
    QVector<XItem> items;
    QString err;
    if (!RetroFiles::readJson(json, &items, &err)) { message("Drop", err); return false; }
    ImportCtx c;
    importItems(items, user, side, c);
    importSummary(c);
    return c.errors.isEmpty();
}

static XItem hostItem(const QString &path, QStringList *errors, int depth = 0) {
    QFileInfo fi(path);
    XItem x;
    x.name = fi.fileName();
    if (fi.isDir()) {
        x.isDir = true;
        if (depth > 8) return x;
        QDir d(path);
        for (const QFileInfo &k : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            x.kids << hostItem(k.absoluteFilePath(), errors, depth + 1);
        return x;
    }
    QFile f(path);
    if (fi.size() > 16 * 1024 * 1024) { errors->append(fi.fileName() + ": too large for a CP/M disc"); x.isDir = true; return x; }
    if (!f.open(QIODevice::ReadOnly)) { errors->append(fi.fileName() + ": " + f.errorString()); x.isDir = true; return x; }
    x.data = f.readAll();
    x.when = fi.lastModified();
    return x;
}

void MainWindow::importPaths(const QStringList &paths, int user, int side) {
    if (!writable()) { message("Add files", "The disc is read-only: " + readOnlyReason()); return; }
    ImportCtx c;
    QVector<XItem> items;
    for (const QString &p : paths) {
        if (QFileInfo(p).fileName().endsWith(".inf", Qt::CaseInsensitive)) continue;   // Acorn sidecars: not CP/M files
        items << hostItem(p, &c.errors);
    }
    importItems(items, user, side, c);
    importSummary(c);
}

void MainWindow::importItems(const QVector<XItem> &items, int user, int side, ImportCtx &c) {
    side = qBound(0, side, qMax(0, m_nvols - 1));
    if (!writable()) { c.errors << "The disc is read-only: " + readOnlyReason(); return; }
    for (const XItem &x : items) {
        if (c.cancelled) return;
        if (x.isDir) {
            int u = userFromFolderName(x.name);
            if (u > maxUser()) u = -1;
            if (u < 0 && !x.kids.isEmpty()) c.flattened++;
            importItems(x.kids, u >= 0 ? u : user, side, c);
            continue;
        }
        importOne(x, user, side, c);
    }
}

void MainWindow::importOne(const XItem &x, int targetUser, int side, ImportCtx &c) {
    Cpm::Volume &vol = m_vols[side];
    int user = targetUser >= 0 ? targetUser : (x.user >= 0 ? x.user : 0);
    if (user > maxUser()) user = 0;
    QString why, name = x.name.toUpper();
    if (!Cpm::Volume::validName(name, &why)) {
        name = Cpm::Volume::toCpmName(x.name);
        c.renamed << QString("%1 -> %2").arg(x.name, name);
    }
    quint16 attrs = quint16((x.readOnly ? Cpm::AttrRO : 0) | (x.system ? Cpm::AttrSYS : 0) | (x.archive ? Cpm::AttrARC : 0) |
                            (x.interfaceAttrs & 0x0F) << 4);
    bool replace = false;
    int ix = vol.indexOf(user, name);
    if (ix >= 0) {
        int choice = c.policy;
        if (choice == 0) {
            if (autoAnswer) choice = 1;
            else {
                QMessageBox box(QMessageBox::Question, "File exists", QString("%1 already exists in user %2%3. Replace it?").arg(name).arg(user).arg(m_nvols > 1 ? QString(" on side %1").arg(side) : QString()),
                                QMessageBox::NoButton, this);
                QPushButton *rep = box.addButton("Replace", QMessageBox::AcceptRole);
                QPushButton *repAll = box.addButton("Replace all", QMessageBox::AcceptRole);
                QPushButton *skip = box.addButton("Skip", QMessageBox::RejectRole);
                QPushButton *skipAll = box.addButton("Skip all", QMessageBox::RejectRole);
                box.addButton(QMessageBox::Cancel);
                box.exec();
                if (box.clickedButton() == rep) choice = 1;
                else if (box.clickedButton() == repAll) choice = c.policy = 1;
                else if (box.clickedButton() == skip) choice = 2;
                else if (box.clickedButton() == skipAll) choice = c.policy = 2;
                else { c.cancelled = true; return; }
            }
        }
        if (choice == 2) { c.skipped++; return; }
        replace = true;
        if (vol.files()[ix].attrs & Cpm::AttrRO) {      // the user chose to replace a read-only file: clear R/O first
            QString e;
            if (!vol.setAttrs(user, name, vol.files()[ix].attrs & ~Cpm::AttrRO, &e)) { c.errors << e; return; }
        }
    }
    QString err;
    if (!vol.addFile(user, name, x.data, attrs, x.when, replace, &err)) { c.errors << err; return; }
    c.files++;
    if (replace) c.replaced++;
}

void MainWindow::importSummary(const ImportCtx &c) {
    refresh();
    QString s = QString("Added %1 file%2").arg(c.files).arg(c.files == 1 ? "" : "s");
    if (c.replaced) s += QString(" (%1 replaced)").arg(c.replaced);
    if (c.skipped) s += QString(", skipped %1").arg(c.skipped);
    if (c.flattened) s += QString(", %1 folder%2 flattened (CP/M has no folders)").arg(c.flattened).arg(c.flattened == 1 ? "" : "s");
    if (!c.renamed.isEmpty()) s += QString(", %1 renamed").arg(c.renamed.size());
    m_status->setText(s + ".");
    QStringList detail = c.errors;
    if (!c.renamed.isEmpty() && c.renamed.size() <= 20) detail << "" << "Renamed to CP/M 8.3 names:" << c.renamed;
    if (!c.errors.isEmpty()) message("Add files", s + ".\n\n" + detail.join("\n"));
}

void MainWindow::addFilesDialog() {
    QStringList p = QFileDialog::getOpenFileNames(this, "Add files", AppSettings().value("addDir").toString());
    if (p.isEmpty()) return;
    AppSettings().setValue("addDir", QFileInfo(p.first()).absolutePath());
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    importPaths(p, sel.isEmpty() ? -1 : targetUser(sel.first()), sel.isEmpty() ? 0 : targetSide(sel.first()));
}

// ------------------------------------------------------------------------------------------- copy and paste

// Edit > Copy takes a copy of the selected files there and then, so a paste copies what was selected at the time even
// if the disc has changed or its window has closed since. Inside this program a paste uses that copy (the clipboard
// carries a token naming it); for the desktop and other programs the clipboard offers the files, as a drag does
// (text/uri-list, the retro-files JSON and GNOME's list), written to a temporary folder only when something asks.
static QVector<XItem> &clipItems() { static QVector<XItem> v; return v; }
static QByteArray &clipToken() { static QByteArray t; return t; }

void MainWindow::copySelected() {
    QVector<FileRef> refs = selectedRefs();
    if (refs.isEmpty()) { m_status->setText("Select the files to copy."); return; }
    QVector<XItem> items;
    QString err;
    if (!exportFiles(refs, &items, &err)) { message("Copy", err); return; }
    static int serial = 0;
    clipItems() = items;
    clipToken() = QByteArray::number(qint64(QCoreApplication::applicationPid())) + ":" + QByteArray::number(++serial);
    auto *m = new RetroFiles::DragOutMime([items](QVector<XItem> *out, QString *) { *out = items; return true; },
                                          trimTextOnExtract(), QFileInfo(m_img.path()).fileName());
    m->setForClipboard(true);
    m->setData(CPMDW_MIME_CLIP, clipToken());
    QApplication::clipboard()->setMimeData(m);
    static bool once = false;                  // the copied files live in a folder removed at exit: don't leave
    if (!once) {                               // a clipboard behind that names them
        once = true;
        connect(qApp, &QCoreApplication::aboutToQuit, [] {
            const QMimeData *c = QApplication::clipboard()->mimeData();
            if (c && c->hasFormat(CPMDW_MIME_CLIP) && QApplication::clipboard()->ownsClipboard()) QApplication::clipboard()->clear();
        });
    }
    m_status->setText(QString("Copied %1 file%2 - Edit > Paste puts %3 into another disc, a folder on the desktop or "
                              "another program.").arg(items.size()).arg(items.size() == 1 ? "" : "s").arg(items.size() == 1 ? "it" : "them"));
}

// Local files the clipboard names: a file manager's copy (text/uri-list, or GNOME's own list).
static QStringList clipboardPaths(const QMimeData *m) {
    QStringList paths;
    for (const QUrl &u : m->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (paths.isEmpty() && m->hasFormat("x-special/gnome-copied-files")) {
        const QList<QByteArray> lines = m->data("x-special/gnome-copied-files").split('\n');
        for (int i = 1; i < lines.size(); ++i) {               // line 0 is "copy" or "cut"
            QUrl u = QUrl::fromEncoded(lines[i].trimmed());
            if (u.isLocalFile()) paths << u.toLocalFile();
        }
    }
    return paths;
}

bool MainWindow::clipboardUsable() const {
    const QMimeData *m = QApplication::clipboard()->mimeData();
    return m && (m->hasFormat(CPMDW_MIME_CLIP) || m->hasFormat(RetroFiles::MimeType) || m->hasUrls() ||
                 m->hasFormat("x-special/gnome-copied-files"));
}

// Edit > Paste: to the user number and side of the selection, as Add files does (no selection: each file keeps its
// own user number).
void MainWindow::paste() {
    if (!m_nvols) return;
    if (!writable()) { message("Paste", "The disc is read-only: " + readOnlyReason()); return; }
    QList<QTreeWidgetItem *> sel = m_tree->selectedItems();
    int user = sel.isEmpty() ? -1 : targetUser(sel.first()), side = sel.isEmpty() ? 0 : targetSide(sel.first());
    const QMimeData *m = QApplication::clipboard()->mimeData();
    if (!m || !pasteItems(m, user, side))
        m_status->setText("Nothing to paste: copy some files first (here, in another window, or in a file manager).");
}

bool MainWindow::pasteItems(const QMimeData *m, int user, int side) {
    if (m->hasFormat(CPMDW_MIME_CLIP) && !clipToken().isEmpty() && m->data(CPMDW_MIME_CLIP) == clipToken()) {
        const QVector<XItem> items = clipItems();          // copied in this program: nothing written to the host
        ImportCtx c;
        importItems(items, user, side, c);
        importSummary(c);
        return true;
    }
    if (m->hasFormat(RetroFiles::MimeType)) {             // another copy of this program, or another tool
        QByteArray json = m->data(RetroFiles::MimeType);
        if (!json.isEmpty()) { importRetroFiles(json, user, side); return true; }
    }
    QStringList paths = clipboardPaths(m);                 // files copied in a file manager: as Add files
    if (paths.isEmpty()) return false;
    importPaths(paths, user, side);
    return true;
}

// ------------------------------------------------------------------------------------------- changing files

bool MainWindow::deleteFiles(const QVector<FileRef> &refs, QString *err) {
    if (!writable()) { *err = "read-only: " + readOnlyReason(); return false; }
    auto snap = m_img.snapshot();
    for (const FileRef &r : refs) {
        Cpm::Volume &v = m_vols[qBound(0, r.side, 1)];
        auto undo = [&] { m_img.restore(snap); for (int k = 0; k < m_nvols; ++k) m_vols[k].reload(); };
        int i = v.indexOf(r.user, r.name);
        if (i >= 0 && (v.files()[i].attrs & Cpm::AttrRO)) { *err = r.name + " is read-only"; undo(); return false; }
        if (!v.deleteFile(r.user, r.name, err)) { undo(); return false; }
    }
    return true;
}

void MainWindow::deleteSelected() {
    QVector<FileRef> refs = selectedRefs();
    if (refs.isEmpty()) return;
    if (!autoAnswer &&
        QMessageBox::question(this, "Delete", refs.size() == 1 ? QString("Delete %1:%2?").arg(refs[0].user).arg(refs[0].name)
                                                               : QString("Delete %1 files?").arg(refs.size())) != QMessageBox::Yes)
        return;
    QString err;
    if (!deleteFiles(refs, &err)) message("Delete", "Nothing was deleted: " + err);
    refresh();
}

void MainWindow::renameSelected() {
    QVector<FileRef> refs = selectedRefs();
    if (refs.size() != 1) return;
    bool ok;
    QString n = QInputDialog::getText(this, "Rename", QString("New name for %1 (8.3):").arg(refs[0].name), QLineEdit::Normal, refs[0].name, &ok);
    if (!ok || n.trimmed().isEmpty()) return;
    QString err;
    if (!m_vols[refs[0].side].rename(refs[0].user, refs[0].name, refs[0].user, n.trimmed(), &err)) message("Rename", err);
    refresh();
}

void MainWindow::editProperties() {
    QVector<FileRef> refs = selectedRefs();
    if (refs.isEmpty()) return;
    QDialog d(this);
    d.setWindowTitle(refs.size() == 1 ? "Properties - " + refs[0].name : QString("Properties - %1 files").arg(refs.size()));
    auto *fl = new QFormLayout;
    const Cpm::File *one = refs.size() == 1 ? &m_vols[refs[0].side].files()[m_vols[refs[0].side].indexOf(refs[0].user, refs[0].name)] : nullptr;
    QLineEdit *name = nullptr;
    if (one) {
        name = new QLineEdit(one->name);
        fl->addRow("Name", name);
        fl->addRow("Size", new QLabel(QString("%1 bytes (%2 records%3), %4 block%5, %6 directory entr%7")
                                          .arg(one->size).arg(one->records).arg(one->exactSize ? ", exact size recorded" : "")
                                          .arg(one->blocks).arg(one->blocks == 1 ? "" : "s").arg(one->entries.size())
                                          .arg(one->entries.size() == 1 ? "y" : "ies")));
        if (one->created.isValid()) fl->addRow("Created", new QLabel(one->created.toString("yyyy-MM-dd HH:mm")));
        if (one->accessed.isValid()) fl->addRow("Accessed", new QLabel(one->accessed.toString("yyyy-MM-dd HH:mm")));
        if (one->updated.isValid()) fl->addRow("Updated", new QLabel(one->updated.toString("yyyy-MM-dd HH:mm")));
        if (!one->problem.isEmpty()) fl->addRow("Problem", new QLabel(one->problem));
    }
    auto *user = new QSpinBox;
    user->setRange(-1, maxUser());
    user->setSpecialValueText("(unchanged)");
    user->setValue(one ? one->user : -1);
    fl->addRow("User number", user);
    struct Box { quint16 bit; const char *label; QCheckBox *cb; };
    QVector<Box> boxes = {{Cpm::AttrRO, "Read-only (T1')", nullptr}, {Cpm::AttrSYS, "System - hidden from DIR (T2')", nullptr},
                          {Cpm::AttrARC, "Archived (T3')", nullptr}, {Cpm::AttrF1, "F1'", nullptr}, {Cpm::AttrF2, "F2'", nullptr},
                          {Cpm::AttrF3, "F3'", nullptr}, {Cpm::AttrF4, "F4'", nullptr}};
    for (Box &b : boxes) {
        b.cb = new QCheckBox(b.label);
        int on = 0;
        for (const FileRef &r : refs) if (m_vols[r.side].files()[m_vols[r.side].indexOf(r.user, r.name)].attrs & b.bit) ++on;
        if (on == 0) b.cb->setCheckState(Qt::Unchecked);
        else if (on == refs.size()) b.cb->setCheckState(Qt::Checked);
        else { b.cb->setTristate(true); b.cb->setCheckState(Qt::PartiallyChecked); }
        fl->addRow(b.cb);
    }
    auto *bb = new QDialogButtonBox(writable() ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel : QDialogButtonBox::Close);
    auto *lay = new QVBoxLayout(&d);
    lay->addLayout(fl);
    if (!writable()) lay->addWidget(new QLabel("Read-only: " + readOnlyReason()));
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted || !writable()) return;
    auto snap = m_img.snapshot();
    QString err;
    bool ok = true;
    for (const FileRef &r : refs) {
        Cpm::Volume &v = m_vols[r.side];
        int i = v.indexOf(r.user, r.name);
        if (i < 0) continue;
        quint16 a = v.files()[i].attrs;
        for (const Box &b : boxes) {
            if (b.cb->checkState() == Qt::Checked) a |= b.bit;
            else if (b.cb->checkState() == Qt::Unchecked) a &= quint16(~b.bit);
        }
        if (ok && a != v.files()[i].attrs) ok = v.setAttrs(r.user, r.name, a, &err);
        QString newName = name ? name->text().trimmed().toUpper() : r.name;
        int newUser = user->value() >= 0 ? user->value() : r.user;
        if (ok && (newName != r.name || newUser != r.user)) ok = v.rename(r.user, r.name, newUser, newName, &err);
    }
    if (!ok) { m_img.restore(snap); for (int k = 0; k < m_nvols; ++k) m_vols[k].reload(); message("Properties", "Nothing was changed: " + err); }
    refresh();
}

void MainWindow::setLabelDialog() {
    bool ok;
    QString n = QInputDialog::getText(this, "Disc label", "Label (CP/M 3, 8.3 characters):", QLineEdit::Normal, m_vols[currentSide()].label().name, &ok);
    if (!ok) return;
    QString err;
    if (!m_vols[currentSide()].setLabel(n, &err)) message("Disc label", err);
    refresh();
}

// ------------------------------------------------------------------------------------------- tools

void MainWindow::checkDisc() {
    QString s;
    bool allOk = true;
    for (int side = 0; side < m_nvols; ++side) {
        const Cpm::Volume &v = m_vols[side];
        Cpm::Report r = v.check();
        allOk = allOk && r.ok();
        if (m_nvols > 1) s += QString("Side %1:\n").arg(side);
        if (r.ok() && r.warnings.isEmpty()) s += "No problems found.\n";
        if (!r.errors.isEmpty()) s += "Errors:\n" + r.errors.join("\n") + "\n";
        if (!r.warnings.isEmpty()) s += "Warnings:\n" + r.warnings.join("\n") + "\n";
        s += QString("%1 files in %2 directory entries; %3 of %4 entries free; %5 of %6 blocks free.\n\n")
                 .arg(v.files().size()).arg(v.usedEntries()).arg(v.freeEntries()).arg(v.format().maxdir)
                 .arg(v.freeBlocks()).arg(v.format().dsm + 1);
    }
    message("Check disc", s.trimmed(), !allOk);
}

void MainWindow::discInfo() {
    QStringList l;
    Disk::Shape sh = m_img.shape();
    l << "<h3>" + QFileInfo(m_img.path()).fileName().toHtmlEscaped() + "</h3>";
    l << "<b>Container:</b> " + Disk::kindName(m_img.kind());
    if (!m_img.headerLine().isEmpty()) l << "<b>ImageDisk header:</b> " + m_img.headerLine().toHtmlEscaped();
    if (!m_img.comment().isEmpty()) l << "<b>Comment / creator:</b> " + m_img.comment().toHtmlEscaped().replace("\n", "<br>");
    l << QString("<b>Physical:</b> %1 cylinders, %2 side(s); %3 tracks of %4 x %5 bytes, sector IDs from %6")
             .arg(m_img.cylinders()).arg(m_img.heads()).arg(sh.uniformTracks).arg(sh.sectrk).arg(sh.seclen).arg(sh.firstId);
    if (sh.oddTracks) l << QString("&nbsp;&nbsp;%1 track(s) of another shape (often a single-density boot track)").arg(sh.oddTracks);
    if (sh.badSectors) l << QString("&nbsp;&nbsp;%1 bad, missing or duplicated sector(s)").arg(sh.badSectors);
    if (m_img.kind() == Disk::Kind::Raw) l << QString("<b>Raw file size:</b> %1 bytes").arg(m_img.rawFileSize());
    if (m_nvols) {
        const Cpm::Format &f = m_vols[0].format();
        l << "<b>Format:</b> " + f.displayName().toHtmlEscaped() + " (" + f.origin.toHtmlEscaped() + ")";
        l << "&nbsp;&nbsp;" + f.summary().toHtmlEscaped();
        if (!f.source.isEmpty()) l << "&nbsp;&nbsp;Source: " + f.source.toHtmlEscaped();
        int bsh = 0;
        while ((128 << bsh) < f.blocksize) ++bsh;
        quint16 al = 0;
        for (int b = 0; b < qMin(16, f.dirBlocks); ++b) al |= quint16(0x8000 >> b);
        l << QString("<b>Disc parameter block:</b> SPT %1, BSH %2, BLM %3, EXM %4, DSM %5, DRM %6, AL0 %7H, AL1 %8H, OFF %9")
                 .arg(f.sectrk * f.seclen / 128).arg(bsh).arg((1 << bsh) - 1).arg(f.exm).arg(f.dsm).arg(f.drm)
                 .arg(al >> 8, 2, 16, QChar('0')).arg(al & 0xFF, 2, 16, QChar('0')).arg(f.bootsec >= 0 ? f.bootsec / f.sectrk : f.boottrk);
        QString xl;
        for (int v : f.xlat) xl += QString::number(v + 1) + " ";
        l << "<b>Sector order on a track:</b> " + xl;
        for (int side = 0; side < m_nvols; ++side) {
            const Cpm::Volume &v = m_vols[side];
            l << QString("<b>Directory%5:</b> %1 of %2 entries used; %3; label %4")
                     .arg(v.usedEntries()).arg(f.maxdir).arg(v.hasStamps() ? "time stamps: " + v.stampKinds() : QString("no time stamps"))
                     .arg(v.label().present ? "'" + v.label().name.toHtmlEscaped() + "'" : QString("none"))
                     .arg(m_nvols > 1 ? QString(" (side %1)").arg(side) : QString());
        }
        l << QString("<b>Detection:</b> confidence %1. %2").arg(m_det.confidence, m_det.why.toHtmlEscaped());
        l << "<b>As a cpmtools definition:</b><pre>" + f.toDiskdef().toHtmlEscaped() + "</pre>";
    }
    if (autoAnswer) { m_status->setText("info shown"); return; }
    QDialog d(this);
    d.setWindowTitle("Disc information");
    d.resize(720, 560);
    auto *tb = new QTextBrowser;
    tb->setHtml(l.join("<br>"));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *lay = new QVBoxLayout(&d);
    lay->addWidget(tb);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.exec();
}

// The readings as the disc is now: files may have been added or deleted since it was detected, and a disc made
// here was scored when it was empty. (Tools > Disc format needs no unsaved changes, so this is the saved disc.)
void MainWindow::rescoreReadings() {
    Disk::RawLayout before = m_img.rawLayout();
    QString e;
    for (Detect::Candidate &c : m_det.ranked) {
        if (m_img.kind() == Disk::Kind::Raw && c.raw.valid() && !m_img.setRawLayout(c.raw, &e)) continue;
        c = Detect::reevaluate(m_img, c);
    }
    if (m_img.kind() == Disk::Kind::Raw && before.valid()) m_img.setRawLayout(before, &e);
}

void MainWindow::formatDialog() {
    if (!m_img.isOpen()) return;
    if (m_img.hasPending()) { message("Disc format", "Save or revert your changes before reading the disc with another format."); return; }
    Disk::RawLayout before = m_img.rawLayout();
    rescoreReadings();
    FormatDialog d(&m_img, m_det, m_detIndex, m_safe, this);
    int r = d.exec();
    QString err;
    if (m_img.kind() == Disk::Kind::Raw && before.valid()) m_img.setRawLayout(before, &err);   // previews may change it
    if (r != QDialog::Accepted) { for (int side = 0; side < m_nvols; ++side) m_vols[side].reload(); return; }
    Detect::Candidate c = d.chosen();
    if (!c.fmt.derived) return;
    useFormat(c, d.allowChanges());
}

void MainWindow::addDiskdefsDialog() {
    QString p = QFileDialog::getOpenFileName(this, "Add a cpmtools diskdefs file", QString(), "diskdefs (diskdefs* *.def *.txt);;All files (*)");
    if (p.isEmpty()) return;
    AppSettings st;
    QStringList files = st.value("diskdefFiles").toStringList();
    if (!files.contains(p)) files << p;
    QStringList warnings;
    Cpm::setExtraDiskdefFiles(files, &warnings);
    st.setValue("diskdefFiles", files);
    message("Format library", QString("The library now has %1 formats.%2").arg(Cpm::library().size())
                                  .arg(warnings.isEmpty() ? QString() : "\n\n" + warnings.mid(0, 20).join("\n")), !warnings.isEmpty());
}

void MainWindow::about() {
    QMessageBox::about(this, "About CP/M Disk Wrangler",
                       "<b>CP/M Disk Wrangler</b><br>Version " + QApplication::applicationVersion().toHtmlEscaped() +
                       "<br><br>View and edit CP/M disc images: raw sector dumps, ImageDisk (.IMD), "
                       "CPCEMU and extended DSK, and TeleDisk (.TD0, read-only). Works out each disc's format from its contents, "
                       "and exchanges files with Acorn Disk Wrangler and other tools by drag and drop (retro-files format).<br><br>"
                       "Copyright &copy; 2026 Daniel Jameson.<br>Licensed under the GNU General Public License v3 or later.<br><br>"
                       "Includes the disc definitions from cpmtools 2.23 (Michael Haardt and contributors, GPL v3 or later). "
                       "The TeleDisk decoder follows LZHUF by Haruyasu Yoshizaki and Haruhiko Okumura. Uses Qt under the LGPL v3. "
                       "Help &gt; Licences shows the full licence texts and notices, including those of the libraries distributed with it.<br><br>" +
                           AppSettings::where().toHtmlEscaped());
}
