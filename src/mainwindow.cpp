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
#include "cpmvolume.h"
#include <diskwrangler/ui/handoffdialog.h>
#include <diskwrangler/app/appsettings.h>

#include <diskwrangler/ui/buttonbar.h>
#include <diskwrangler/ui/licences.h>
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
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

// ================================================================================================ DropTree

static QByteArray sourceTag(quint64 owner) {
    return QByteArray::number(qint64(QCoreApplication::applicationPid())) + ":" + QByteArray::number(owner);
}

DropTree::DropTree(QWidget *parent) : dw::ui::VolumePane(parent) {
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(true);
}

QStringList DropTree::mimeTypes() const { return {DW_MIME_ITEMS, RetroFiles::MimeType, "text/uri-list"}; }
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
    if (!m->hasFormat(DW_MIME_SOURCE) || !m->hasFormat(DW_MIME_ITEMS)) return false;
    QList<QByteArray> f = m->data(DW_MIME_SOURCE).split(':');
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
    dw::DragOutMime::Exporter exporter = [pw, unique](QVector<dw::TransferItem> *out, QString *err) {
        if (!pw) { *err = "the disc's window was closed during the drag"; return false; }
        QVector<MainWindow::Item> items;
        for (const FileRef &r : unique) items << MainWindow::itemOf(r);
        return pw->exportForCopy(items, out, err);
    };
    auto *m = new dw::DragOutMime(exporter, RetroFiles::dragSpec(win->trimTextOnExtract(), QFileInfo(win->image().path()).fileName()));
    m->setData(DW_MIME_ITEMS, payload);
    m->setData(DW_MIME_SOURCE, sourceTag(m_owner));
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
        if (m->data(DW_MIME_SOURCE) != sourceTag(m_owner)) return true;
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
        emit internalDrop(e->mimeData()->data(DW_MIME_ITEMS), itemAt(dropPos(e)));
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

MainWindow *MainWindow::byId(quint64 id) { return qobject_cast<MainWindow *>(DiscWindow::byId(id)); }

QList<MainWindow *> MainWindow::windows() {
    QList<MainWindow *> l;
    for (DiscWindow *w : DiscWindow::all()) if (auto *m = qobject_cast<MainWindow *>(w)) l << m;
    return l;
}

dw::app::AppInfo MainWindow::appInfo() const { return Cpm::appInfo(QApplication::applicationVersion()); }

dw::ui::DiscWindow::ActionSpec MainWindow::actionSpec(Act a) const {
    auto key = [](QKeySequence::StandardKey k) { return QList<QKeySequence>{QKeySequence(k)}; };
    switch (a) {
    case Act::NewWindow: return {"New &window", {QKeySequence("Ctrl+Shift+N")}, QString(), QString()};
    case Act::NewImage: return {"&New disc image...", key(QKeySequence::New), QString(), QString()};
    case Act::Open: return {"&Open...", key(QKeySequence::Open), QString(), QString()};
    case Act::Save: return {"&Save", key(QKeySequence::Save), QString(), QString()};
    case Act::SaveAs: return {"Save &as (ImageDisk, DSK or raw)...", {QKeySequence("Ctrl+Shift+S")}, QString(), QString()};
    case Act::Revert: return {"&Revert", {}, QString(), QString()};
    case Act::Close: return {"&Close", key(QKeySequence::Close), QString(), QString()};
    case Act::Quit: return {"&Quit", key(QKeySequence::Quit), QString(), QString()};
    case Act::View: return {"&View (text / hex)", {QKeySequence("F3")}, "View", QString()};
    case Act::Extract: return {"E&xtract to folder...", {QKeySequence("Ctrl+E")}, "Extract", QString()};
    case Act::AddFiles: return {"&Add files...", {QKeySequence("Ctrl+Shift+F")}, "Add files", QString()};
    case Act::Copy: return {"&Copy", QKeySequence::keyBindings(QKeySequence::Copy), QString(), QString()};   // the platform's own keys (Ctrl+C, Ctrl+Ins / Cmd+C)
    case Act::Paste: return {"&Paste", QKeySequence::keyBindings(QKeySequence::Paste), QString(), QString()}; // (Ctrl+V, Shift+Ins / Cmd+V)
    case Act::Rename: return {"Re&name...", {QKeySequence("F2")}, "Rename", QString()};
    case Act::Properties: return {"P&roperties (attributes, user)...", {QKeySequence("Alt+Return")}, "Properties", QString()};
    case Act::Delete: return {"&Delete", dw::deleteKeys(), "Delete", QString()};
    default: return {};                // no Add folder or New directory: CP/M has no folders
    }
}

dw::ui::PaneLayout MainWindow::paneLayout(const QString &) const {
    dw::ui::PaneLayout l;
    l.columns = {{"Name", "", false}, {"Attr", "R/O SYS ARC", false}, {"Size", "1048576", true},
                 {"Blocks", "00000", true}, {"Date", "2026-12-31 23:59", false}};
    l.expand = dw::ui::PaneLayout::ExpandAll;           // every side and user area open
    l.reselectFolders = false;                          // after a refresh, only the files stay selected
    return l;
}

dw::ui::VolumePane *MainWindow::createPane(const QString &) {
    auto *t = new DropTree;
    t->setOwner(id());
    return t;
}

QList<QAction *> MainWindow::contextActions() const {     // the Edit menu's order, as Acorn Disk Wrangler's
    return {m_actView, m_actExtract, nullptr, m_actAdd, nullptr, m_actCopy, m_actPaste, nullptr, m_actRename, m_actProps, m_actDelete};
}

MainWindow::MainWindow(QWidget *parent) : DiscWindow(parent) {
    setupKit();                                         // the pane and the standard actions
    m_actSave = action(Act::Save);
    m_actSaveAs = action(Act::SaveAs);
    m_actRevert = action(Act::Revert);
    m_actView = action(Act::View);
    m_actExtract = action(Act::Extract);
    m_actAdd = action(Act::AddFiles);
    m_actCopy = action(Act::Copy);
    m_actPaste = action(Act::Paste);
    m_actRename = action(Act::Rename);
    m_actProps = action(Act::Properties);
    m_actDelete = action(Act::Delete);
    m_status = new StatusLabel;
    statusBar()->addWidget(m_status, 1);
    m_actLower = new QAction("Allow &lower-case file names", this);     // in Tools and, as a button, in the status bar
    m_actLower->setCheckable(true);
    m_actLower->setToolTip("CP/M upper-cases names; some discs (COMAL's) hold lower-case ones. Ticked for a disc that has any: names added "
                           "keep their case, so they go back exactly as they came out. Unticked, names are made upper case.");
    connect(m_actLower, &QAction::toggled, this, [this](bool on) { setLowerNames(on, true); });
    m_natBtn = new QToolButton;
    m_natBtn->setAutoRaise(true);
    m_natBtn->setVisible(false);
    m_natBtn->setPopupMode(QToolButton::InstantPopup);                   // its menu is added with the Tools menu items below
    m_natBtn->setStyleSheet("QToolButton::menu-indicator { image: none; }");
    statusBar()->addPermanentWidget(m_natBtn);
    resize(860, 600);

    // The menus, the context menu and the button bar keep the same order as Acorn Disk Wrangler's (which has a few
    // more items: Add folder, New directory, Boot option, Side order).
    QMenu *file = menuBar()->addMenu("&File");
    file->addActions({action(Act::NewWindow), action(Act::NewImage), action(Act::Open)});
    file->addSeparator();
    file->addActions({m_actSave, m_actSaveAs, m_actRevert});
    file->addSeparator();
    AppSettings::addMenuItem(file, this);         // where portable settings exist (not a Linux install)
    if (AppSettings::offered()) file->addSeparator();
    file->addActions({action(Act::Close), action(Act::Quit)});

    QMenu *edit = menuBar()->addMenu("&Edit");
    edit->addActions({m_actView, m_actExtract});
    edit->addSeparator();
    edit->addAction(m_actAdd);
    edit->addSeparator();
    edit->addActions({m_actCopy, m_actPaste});
    edit->addSeparator();
    edit->addActions({m_actRename, m_actProps, m_actDelete});
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
    m_actNat = tools->addAction("&Character set for names and text...", this, &MainWindow::natDialog);
    {   // the same two items as a menu on the status bar button
        auto *nm = new QMenu(m_natBtn);
        nm->addAction(m_actLower);
        nm->addAction(m_actNat);
        m_natBtn->setMenu(nm);
    }
    m_actNat->setToolTip("The national letters ([ \\ ] { | } for \u00C6\u00D8\u00C5 \u00E6\u00F8\u00E5 and so on) this disc's names and text use");
    tools->addAction("Add a &diskdefs file to the format library...", this, &MainWindow::addDiskdefsDialog);
    tools->addSeparator();
    AppSettings st;
    tools->addAction(m_actLower);
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
    connect(tree(), &DropTree::pathsDropped, this, &MainWindow::onPathsDropped);
    connect(tree(), &DropTree::internalDrop, this, &MainWindow::onInternalDrop);
    connect(tree(), &DropTree::retroFilesDropped, this, &MainWindow::onRetroFilesDropped);
    connect(tree(), &DropTree::dragOutFinished, this, [this](int files, const QStringList &errors) {
        if (!errors.isEmpty()) message("Drag", errors.join("\n"));
        else m_status->setText(QString("Copied %1 file%2 out of the disc.").arg(files).arg(files == 1 ? "" : "s"));
    });
    refresh();
}

MainWindow::~MainWindow() = default;

void MainWindow::presentStatus(const QStringList &variants) { if (m_status) m_status->setVariants(variants); }
void MainWindow::showMessage(const QString &text) { if (m_status) m_status->setText(text); }

bool MainWindow::trimTextOnExtract() const { return m_optTrim->isChecked(); }

int MainWindow::maxUser() const { return vol(0).format().os == Cpm::Os::V3 ? 15 : 31; }

void MainWindow::detachAll() { m_doc.detach(); }

QVector<const Cpm::Volume *> MainWindow::volumePtrs() const {
    QVector<const Cpm::Volume *> v;
    for (int i = 0; i < sides(); ++i) v << &vol(i);
    return v;
}

void MainWindow::applyNameRules() {
    for (int side = 0; side < sides(); ++side)
        if (auto *v = dynamic_cast<Cpm::CpmVolume *>(m_doc.disc().volume(side))) v->setCharset(m_natId, lowerDisc());
}

const Cpm::File *MainWindow::fileOf(int side, int user, const QString &name) const {
    for (const Cpm::File &f : vol(side).files()) if (f.user == user && f.name == name) return &f;
    return nullptr;
}

int MainWindow::currentSide() const {
    QList<QTreeWidgetItem *> sel = tree()->selectedItems();
    return sel.isEmpty() ? 0 : qBound(0, sel.first()->data(0, RoleSide).toInt(), qMax(0, sides() - 1));
}

// ------------------------------------------------------------------------------------------- opening

QString MainWindow::rememberedFormat() const {
    AppSettings st;
    QString key = "formats/" + QCryptographicHash::hash(QFileInfo(image().path()).canonicalFilePath().toUtf8(),
                                                        QCryptographicHash::Sha1).toHex();
    QString v = st.value(key).toString();
    qint64 size = v.section('|', 0, 0).toLongLong();
    return size == QFileInfo(image().path()).size() ? v.section('|', 1) : QString();
}

void MainWindow::rememberFormat() {
    if (image().path().isEmpty() || !sides()) return;
    AppSettings st;
    QString key = "formats/" + QCryptographicHash::hash(QFileInfo(image().path()).canonicalFilePath().toUtf8(),
                                                        QCryptographicHash::Sha1).toHex();
    Cpm::Format f = vol(0).format();
    st.setValue(key, QString::number(QFileInfo(image().path()).size()) + "|" + f.toDiskdef());
}

QString MainWindow::rememberedNat() const {
    if (image().path().isEmpty()) return QString();
    return AppSettings().value("charset/" + QCryptographicHash::hash(QFileInfo(image().path()).canonicalFilePath().toUtf8(),
                                                                     QCryptographicHash::Sha1).toHex()).toString();
}

static QString diskKey(const QString &what, const QString &path) {
    return what + "/" + QCryptographicHash::hash(QFileInfo(path).canonicalFilePath().toUtf8(), QCryptographicHash::Sha1).toHex();
}

void MainWindow::autoLower() {
    bool on = Nat::usesLowerCase(volumePtrs());
    if (!image().path().isEmpty()) {
        const QVariant v = AppSettings().value(diskKey("lowercase", image().path()));
        if (v.isValid()) on = v.toBool();
    }
    m_lowerOk = on;
    showLower(on);
}

void MainWindow::refreshNamesButton() {      // "Names: ASCII, upper case": which character set and case rule names are shown with
    const Nat::Set ns = Nat::find(m_natId);
    const bool suspect = m_natId == "ascii" && !m_natHint.isEmpty();
    const QString setName = ns.id == "ascii" ? QStringLiteral("ASCII") : ns.name.section("  (", 0, 0);
    m_natBtn->setVisible(sides() > 0);
    m_natBtn->setText(QString("Names: %1, %2%3").arg(setName, m_lowerOk ? "any case" : "upper case", suspect ? " (national letters?)" : ""));
    m_natBtn->setToolTip((suspect ? "This disc's " + m_natHint + ".\n" : QString()) +
                         QString("Names and text are shown as %1; file names are %2. Click for the character set and lower-case options.")
                             .arg(ns.id == "ascii" ? "plain ASCII, just as the disc holds them" : ns.name,
                                  m_lowerOk ? "kept in the case you give them (lower case allowed)" : "made upper case"));
}

void MainWindow::showLower(bool on) {            // the Tools item and the status bar tick box, without acting on the change
    QSignalBlocker a(m_actLower);
    m_actLower->setChecked(on);
    refreshNamesButton();
}

void MainWindow::setLowerNames(bool on, bool remember) {
    m_lowerOk = on;
    showLower(on);
    applyNameRules();
    if (remember && !image().path().isEmpty()) AppSettings().setValue(diskKey("lowercase", image().path()), on);
    refresh();
}

void MainWindow::setNat(const QString &id, bool remember) {
    m_natId = Nat::find(id).id;
    applyNameRules();
    if (remember && !image().path().isEmpty()) {
        AppSettings st;
        const QString key = "charset/" + QCryptographicHash::hash(QFileInfo(image().path()).canonicalFilePath().toUtf8(),
                                                                  QCryptographicHash::Sha1).toHex();
        if (m_natId == "ascii") st.remove(key); else st.setValue(key, m_natId);
    }
    if (m_natId != "ascii") m_natHint.clear();
    refresh();
}

void MainWindow::natDialog() {
    if (!sides()) return;
    QVector<const Cpm::Volume *> vols;
    for (int side = 0; side < sides(); ++side) vols << &vol(side);
    Nat::Dialog d(vols, m_natId, this);
    d.setLowerNames(m_lowerOk);
    if (d.exec() == QDialog::Accepted) setNat(d.chosen(), true);
}

bool MainWindow::attachFormat(const Detect::Candidate &c, QString *err) {
    detachAll();
    if (!m_doc.attach(c, err)) {
        if (err->startsWith("side 0: ")) *err = err->mid(8);     // as this window has always said it: only side 1 is named
        detachAll();
        return false;
    }
    applyNameRules();
    m_current = c;
    return true;
}

bool MainWindow::openImage(const QString &path) { return openFile(path); }

// The open pipeline (the kit's openFile): the file read and detected here, the verdict decided by the kit (asked
// about, or handed to Acorn Disk Wrangler), the reading attached here.
bool MainWindow::detectImage(const QString &path, int, dw::fs::Verdict *verdict) {
    QString err;
    if (!m_doc.load(path, &err)) { message("Open", QString("%1:\n%2").arg(path, err)); return false; }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_det = Detect::detect(image(), rememberedFormat());
    QApplication::restoreOverrideCursor();
    m_doc.setSafe(m_det.safeToWrite);
    m_detIndex = 0;
    // Detection by tiers (DiskWranglerCore): CP/M's reading against a recogniser for Acorn Disk Wrangler's discs. A disc
    // that is plainly Acorn's is opened there; one that reads as CP/M and also looks like Acorn's is asked about.
    *verdict = Cpm::verdict(image(), path, m_det);
    return true;
}

// Not read as CP/M (nothing to attach, or the reading can't be attached): the message as before, with the format dialog
// a button away. The file stays open, so Choose a format... can read it another way.
bool MainWindow::openDocument(const QString &path, const dw::fs::Verdict &, const dw::fs::Candidate *, int) {
    QString err;
    if (m_det.ranked.isEmpty() || !attachFormat(m_det.ranked.first(), &err)) {
        openStopped();
        showNotOpened(path, notOpened(path));
        return false;
    }
    return true;
}

void MainWindow::openStopped() {
    detachAll();
    refresh();
}

dw::ui::DiscWindow::NotOpened MainWindow::notOpened(const QString &path) const {
    NotOpened m;
    m.title = "Open";
    m.text = QString("%1\n\n%2\n%3").arg(QFileInfo(path).fileName(), m_det.physical, detectionText(m_det));
    m.warning = true;
    m.format = Qt::AutoText;
    m.selectable = false;
    auto *self = const_cast<MainWindow *>(this);
    m.buttons = {{"Choose a format...", [self] { self->formatDialog(); }}};
    return m;
}

// Acorn Disk Wrangler wasn't started (not found, or it handed the disc here): CP/M's own message, which the kit gives the
// link to get it and Locate...
dw::ui::DiscWindow::NotOpened MainWindow::handOverMessage(const QString &path, const dw::fs::Verdict &, const dw::fs::Candidate &) const {
    return notOpened(path);
}

void MainWindow::afterOpen(const QString &) {
    if (image().needsPadding()) image().markForSave();
    m_natId = Nat::find(rememberedNat()).id;
    autoLower();
    applyNameRules();
    m_natHint.clear();
    if (m_natId == "ascii") {
        const Nat::Evidence ev = Nat::scan(volumePtrs());
        if (ev.any())
            m_natHint = "names or text use [ \\ ] { | } between letters, which may be national letters";
    }
    refresh();
}

dw::ui::DiscWindow *MainWindow::createWindow() {
    auto *w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
    return w;
}

bool MainWindow::newImage(const QString &path, dw::device::Kind kind, const Cpm::Format &f, bool stamps, const QString &label) {
    QString err;
    dw::device::Container img;
    if (!Cpm::makeBlank(&img, kind, f, stamps, label, &err) || !img.saveAs(path, kind, &err)) {
        message("New disc image", err);
        return false;
    }
    detachAll();
    image() = img;
    m_natId = "ascii";
    m_natHint.clear();
    m_lowerOk = false;                          // a new disc has no lower-case names; forget any choice made for an old file of this name
    AppSettings().remove(diskKey("lowercase", path));
    showLower(false);
    m_det = Detect::Result();
    Detect::Candidate c = Detect::evaluate(image(), f, false);
    c.fmt = f;
    c.fmt.derive(&err);
    c.fromLibrary = true;
    c.raw = image().rawLayout();
    c.evidence << "a new disc made in this format";
    m_det.ranked << c;
    m_det.confidence = "high";
    m_det.why = "A new disc made here.";
    m_det.physical = QString("%1, new").arg(dw::device::kindName(kind));
    m_detIndex = 0;
    m_doc.setSafe(true);
    if (!attachFormat(c, &err)) { message("New disc image", err); return false; }
    m_doc.noteFileState();
    rememberFormat();
    refresh();
    return true;
}

bool MainWindow::useFormat(const Detect::Candidate &c, bool allowChanges) {
    if (image().hasPending()) { message("Disc format", "Save or revert your changes before reading the disc with another format."); return false; }
    QString err;
    if (!attachFormat(c, &err)) {
        message("Disc format", err);
        if (!m_det.ranked.isEmpty()) attachFormat(m_det.ranked.value(m_detIndex, m_det.ranked.first()), &err);
        refresh();
        return false;
    }
    m_detIndex = -1;
    for (int i = 0; i < m_det.ranked.size(); ++i) if (m_det.ranked[i].key() == c.key()) m_detIndex = i;
    m_doc.setSafe(allowChanges);
    if (allowChanges) rememberFormat();
    refresh();
    return true;
}

void MainWindow::newWindow() { createWindow(); }

void MainWindow::openDialog() {
    QString p = QFileDialog::getOpenFileName(this, "Open CP/M disc image", AppSettings().value("lastDir").toString(),
                                             "Disc images (" + dw::anyCase("*.imd *.dsk *.td0 *.img *.ima *.raw *.h8d *.ssd *.dsd *.bin") + ");;All files (*)");
    if (p.isEmpty()) return;
    AppSettings().setValue("lastDir", QFileInfo(p).absolutePath());
    openInWindow(p);                              // in a new window if this one has an image open
}

void MainWindow::newImageDialog() {
    NewDiscDialog d(this);
    if (d.exec() != QDialog::Accepted) return;
    Cpm::Format f = d.format();
    QString ext = dw::device::kindExtension(d.kind());
    QString p = QFileDialog::getSaveFileName(this, "New disc image", AppSettings().value("lastDir").toString() + "/" + f.name + (f.separateSides ? "-2sides" : "") + "." + ext,
                                             QString("Disc image (*.%1)").arg(ext));
    if (p.isEmpty()) return;
    MainWindow *w = image().isOpen() ? static_cast<MainWindow *>(createWindow()) : this;
    w->newImage(p, d.kind(), f, d.stamps(), d.label());
}

void MainWindow::revertDocument() {
    image().discardPending();
    openImage(image().path());
}

// ------------------------------------------------------------------------------------------- saving

bool MainWindow::keepBackup() const { return m_optBackup->isChecked(); }

bool MainWindow::beforeSave() {
    if (image().readOnlyContainer()) { saveAsDialog(); return false; }
    if (!writable()) { message("Save", "The disc is read-only: " + readOnlyReason()); return false; }
    return true;
}

void MainWindow::afterSave() { rememberFormat(); }

bool MainWindow::afterSaveAs(const QString &, QString *err) {
    m_current.raw = image().rawLayout();
    for (int side = 0; side < sides(); ++side)
        if (!m_doc.store(side)->reload(err)) return false;
    if (writable() || m_doc.safe()) rememberFormat();
    return true;
}

bool MainWindow::askSaveAs(QString *path, dw::device::Kind *kind) {
    // .dsk is not only the Amstrad CPC's: emulators of many machines use it for plain sector dumps too. What a file is comes from
    // the filter chosen here, never from the name typed, so the dialog starts on the kind this image already is (and a raw
    // image keeps its own file name, .dsk and all) and the raw filter lists .dsk and the other common raw suffixes.
    const QString imd = "ImageDisk (*.imd)";
    const QString dsk = "Extended DSK image (*.dsk)";
    const QString sdsk = "Standard DSK image (*.dsk)";
    const QString raw = "Raw sector image (*.img *.raw *.bin *.dsk *.h8d *.ima)";
    QString filter = imd;
    QString name = QFileInfo(image().path()).completeBaseName() + ".imd";
    switch (image().kind()) {
    case dw::device::Kind::Raw: filter = raw; name = QFileInfo(image().path()).fileName(); break;
    case dw::device::Kind::Dsk: filter = sdsk; name = QFileInfo(image().path()).completeBaseName() + ".dsk"; break;
    case dw::device::Kind::Edsk: filter = dsk; name = QFileInfo(image().path()).completeBaseName() + ".dsk"; break;
    default: break;
    }
    QString p = QFileDialog::getSaveFileName(this, "Save as", QFileInfo(image().path()).absolutePath() + "/" + name,
                                             imd + ";;" + dsk + ";;" + sdsk + ";;" + raw, &filter);
    if (p.isEmpty()) return false;
    *path = p;
    *kind = filter == imd ? dw::device::Kind::Imd : filter == dsk ? dw::device::Kind::Edsk : filter == sdsk ? dw::device::Kind::Dsk : dw::device::Kind::Raw;
    return true;
}

// ------------------------------------------------------------------------------------------- the tree

QString MainWindow::rootLabel(int side) const {
    const Cpm::Volume &v = vol(side);
    QString title = v.label().present ? toHostName(v.label().name) : (sides() == 1 ? QFileInfo(image().path()).fileName() : QString());
    return dw::rootLabel(sides() == 1 ? QString("A:") : QString("Side %1").arg(side), title);
}

// The rows as this window has always drawn them, with its roles (guitest and screenshot find rows by them): a root per
// side, a row per user area in use with its file count and total size, the files in directory order.
void MainWindow::decorateRow(QTreeWidgetItem *it, int side, const dw::fs::Path &path, const dw::fs::Entry *e) const {
    it->setData(0, RoleSide, side);
    if (!e) {                                           // a side's root
        it->setData(0, RoleKind, RowRoot);
        it->setData(0, RoleUser, -1);
        return;
    }
    if (e->kind == dw::fs::NodeKind::Namespace) {       // a user area
        const int u = Cpm::CpmVolume::userOf(e->name);
        int n = 0;
        qint64 total = 0;
        for (const Cpm::File &f : vol(side).files()) if (f.user == u) { ++n; total += f.size; }
        it->setText(0, QString("User %1   (%2 file%3)").arg(u).arg(n).arg(n == 1 ? "" : "s"));
        it->setData(0, RoleKind, RowUser);
        it->setData(0, RoleUser, u);
        dw::setNumCell(it, 2, QString::number(total));
        return;
    }
    const int u = Cpm::CpmVolume::userOf(path.parts.value(0));
    const Cpm::File *f = fileOf(side, u, e->name);
    if (!f) return;
    it->setText(0, toHostName(f->name));
    it->setData(0, RoleKind, RowFile);
    it->setData(0, RoleUser, f->user);
    it->setData(0, RoleName, f->name);
    it->setText(1, Cpm::attrString(f->attrs));
    dw::setNumCell(it, 2, QString::number(f->size));
    dw::setNumCell(it, 3, QString::number(f->blocks));
    QDateTime d = f->updated.isValid() ? f->updated : f->created;
    if (d.isValid()) it->setText(4, d.toString("yyyy-MM-dd HH:mm"));
    if (!f->problem.isEmpty()) { it->setToolTip(0, f->problem); it->setForeground(0, QColor("#b00000")); }
}

// A side's root stands for its files in directory order (as the window has always copied, extracted and deleted
// them), not user by user.
void MainWindow::filesUnder(int side, const dw::fs::Path &dir, QVector<dw::fs::Path> *out) const {
    if (!dir.isRoot()) { DiscWindow::filesUnder(side, dir, out); return; }
    for (const Cpm::File &f : vol(side).files()) out->append(dw::fs::Path({Cpm::CpmVolume::userName(f.user), f.name}));
}

void MainWindow::updateActions() {
    if (!m_actFormat) return;                           // the constructor hasn't made them all yet
    bool have = sides() > 0;
    bool w = writable();
    QVector<FileRef> refs = have ? selectedRefs() : QVector<FileRef>();
    bool oneFile = tree()->selectedItems().size() == 1 && tree()->selectedItems().first()->data(0, RoleKind).toInt() == RowFile;
    m_actSave->setEnabled(have && image().hasPending() && (w || image().readOnlyContainer()));
    m_actSaveAs->setEnabled(image().isOpen());
    m_actRevert->setEnabled(image().hasPending() && !image().path().isEmpty());
    m_actAdd->setEnabled(w);
    m_actDelete->setEnabled(w && !refs.isEmpty());
    m_actProps->setEnabled(have && !refs.isEmpty());
    m_actRename->setEnabled(w && oneFile);
    m_actLabel->setEnabled(w && vol(currentSide()).labelAllowed());
    m_actExtract->setEnabled(have && !refs.isEmpty());
    m_actCopy->setEnabled(have && !refs.isEmpty());
    m_actPaste->setEnabled(w && clipboardUsable());
    m_actView->setEnabled(have && oneFile);
    m_actCheck->setEnabled(have);
    m_actInfo->setEnabled(image().isOpen());
    m_actNat->setEnabled(have);
    m_actLower->setEnabled(have);
    m_actFormat->setEnabled(image().isOpen());
    refreshNamesButton();
}

QStringList MainWindow::statusVariants() const {
    if (!sides()) {
        if (image().isOpen())
            return {"Not read as CP/M: " + detectionText(m_det) + " Drop another image here to open it.", "Not read as CP/M: " + detectionText(m_det), "Not read as CP/M"};
        return {"Open a disc image (File > Open), or drop one here.", "Open or drop a disc image."};
    }
    const bool w = writable();
    const Cpm::Format &f = vol(0).format();
    const QString name = f.name == "detected" || f.name == "by-hand" ? QString("custom format") : f.name;
    auto files = [](int n) { return QString("%1 file%2").arg(n).arg(n == 1 ? "" : "s"); };
    QString longTail, shortTail;
    if (!w) { longTail += "  -  read-only: " + readOnlyReason(); shortTail += "  -  read-only"; }
    if (image().needsPadding()) { longTail += "  -  the image file is short; it will be padded to full size when saved"; shortTail += "  -  short image"; }
    QStringList v;                                                     // longest first; the widest that fits is shown
    if (sides() == 1) {
        const QString n = files(vol(0).files().size()), fr = dw::fmtBytes(vol(0).freeBytes(), dw::SmallSizes::AsBytes);
        v << QString("%1 - %2, %3 free of %4").arg(name, n, fr, dw::fmtBytes(f.capacityBytes(), dw::SmallSizes::AsBytes)) + longTail
          << QString("%1 - %2, %3 free").arg(name, n, fr) + shortTail
          << QString("%1, %2 free").arg(n, fr) + shortTail
          << QString("%1 free").arg(fr) + shortTail;
    } else {
        QString full = name + " (a disc on each side)", mid, tot;
        int nf = 0;
        qint64 fb = 0;
        for (int side = 0; side < sides(); ++side) {
            full += QString(" - side %1: %2, %3 free").arg(side).arg(files(vol(side).files().size()), dw::fmtBytes(vol(side).freeBytes(), dw::SmallSizes::AsBytes));
            mid += QString("%1side %2: %3, %4 free").arg(side ? " - " : "").arg(side).arg(files(vol(side).files().size()), dw::fmtBytes(vol(side).freeBytes(), dw::SmallSizes::AsBytes));
            nf += vol(side).files().size();
            fb += vol(side).freeBytes();
        }
        v << full + longTail << mid + shortTail << QString("%1, %2 free").arg(files(nf), dw::fmtBytes(fb, dw::SmallSizes::AsBytes)) + shortTail << QString("%1 free").arg(dw::fmtBytes(fb, dw::SmallSizes::AsBytes)) + shortTail;
    }
    return v;
}

QVector<FileRef> MainWindow::selectedRefs() const {
    QVector<FileRef> refs;
    for (const Item &i : selectedFiles()) refs << refOf(i);
    return refs;
}

int MainWindow::targetUser(QTreeWidgetItem *it) const {
    if (!it || it->data(0, RoleKind).toInt() == RowRoot) return -1;
    return it->data(0, RoleUser).toInt();
}

int MainWindow::targetSide(QTreeWidgetItem *it) const {
    return it ? qBound(0, it->data(0, RoleSide).toInt(), qMax(0, sides() - 1)) : 0;
}

// ------------------------------------------------------------------------------------------- copying out

// What View, Copy, a drag and a drop on another window take from the disc: the kit's exportItems (the items `cpmdw get`
// makes), with two things of CP/M Disk Wrangler's. A file that has gone since it was chosen is reported in this window's
// words. And a Torch CP/N file is a "cpm" file for everything that leaves the disc (the retro-files document says
// "cpm", as this window has always sent it), which the kit, being filesystem-blind, doesn't know.
bool MainWindow::exportForCopy(const QVector<Item> &items, QVector<dw::TransferItem> *out, QString *err) const {
    out->clear();
    for (const Item &i : items) {
        const FileRef r = refOf(i);
        if (vol(qBound(0, r.side, 1)).indexOf(r.user, r.name) < 0) {
            *err = QString("%1:%2 is no longer on the disc").arg(r.user).arg(r.name);
            return false;
        }
    }
    if (!exportItems(items, out, err)) return false;
    for (dw::TransferItem &t : *out)
        if (t.sourceFs == "torch") t.sourceFs = "cpm";
    return true;
}

std::shared_ptr<dw::xfer::HostPolicy> MainWindow::hostPolicy(const QVector<dw::TransferItem> &items) const {
    QSet<int> users;
    for (const dw::TransferItem &t : items) users << RetroFiles::CpmPolicy::userOf(t);
    return std::make_shared<RetroFiles::CpmPolicy>(m_optFolders->isChecked() && users.size() > 1, m_optTrim->isChecked());
}

void MainWindow::viewSelected() {
    const QVector<Item> items = selectedFiles();
    if (items.size() != 1) return;
    QVector<dw::TransferItem> files;
    QString err;
    if (!exportForCopy(items, &files, &err)) { message("View", err); return; }
    const FileRef r = refOf(items[0]);
    QString t = QString("%1:%2").arg(r.user).arg(toHostName(r.name));
    if (sides() > 1) t = QString("side %1 ").arg(r.side) + t;
    auto *v = new ViewerDialog(t, files[0].data, this, m_natId);
    v->show();
}

// ------------------------------------------------------------------------------------------- copying in

void MainWindow::onPathsDropped(const QStringList &paths, QTreeWidgetItem *target) {
    QStringList p = paths;
    if (dropOpensImage()) {                       // no disc here yet: open the first, the rest in new windows
        QTimer::singleShot(0, this, [this, p] {
            for (int i = 0; i < p.size(); ++i) {
                if (QFileInfo(p[i]).isDir()) continue;
                if (dropOpensImage()) { openFile(p[i]); continue; }
                createWindow()->openFile(p[i]);
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
        QVector<Item> chosen;
        for (const FileRef &r : refs) chosen << itemOf(r);
        QVector<dw::TransferItem> files;
        QString err;
        if (!w->exportForCopy(chosen, &files, &err)) { message("Copy", err); return; }
        importInto(files, u, side);
    });
}

bool MainWindow::importRetroFiles(const QByteArray &json, int user, int side) {
    QVector<dw::TransferItem> items;
    QString err;
    if (!dw::xfer::readJson(json, &items, &err)) { message("Drop", err); return false; }
    return importInto(items, user, side).errors.isEmpty();
}

// A host file or folder as an item to import. A file that can't be read (or is too large) is left out, with its error.
static bool hostItem(const QString &path, dw::TransferItem *x, QVector<dw::xfer::ImportError> *errors, int depth = 0) {
    QFileInfo fi(path);
    x->name = fi.fileName();
    if (fi.isDir()) {
        x->isDir = true;
        if (depth > 8) return true;
        QDir d(path);
        for (const QFileInfo &k : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            dw::TransferItem kid;
            if (hostItem(k.absoluteFilePath(), &kid, errors, depth + 1)) x->kids << kid;
        }
        return true;
    }
    auto failed = [&](const QString &why) {
        dw::xfer::ImportError e;
        e.kind = dw::xfer::ImportError::Failed;
        e.message = e.why = fi.fileName() + ": " + why;
        e.from = path;
        errors->append(e);
        return false;
    };
    QFile f(path);
    if (fi.size() > 16 * 1024 * 1024) return failed("too large for a CP/M disc");
    if (!f.open(QIODevice::ReadOnly)) return failed(f.errorString());
    x->data = f.readAll();
    x->when = fi.lastModified();
    return true;
}

void MainWindow::importPaths(const QStringList &paths, int user, int side) {
    if (!writable()) { message("Add files", "The disc is read-only: " + readOnlyReason()); return; }
    QVector<dw::xfer::ImportError> errors;
    QVector<dw::TransferItem> items;
    for (const QString &p : paths) {
        if (QFileInfo(p).fileName().endsWith(".inf", Qt::CaseInsensitive)) continue;   // Acorn sidecars: not CP/M files
        dw::TransferItem x;
        if (hostItem(p, &x, &errors)) items << x;
    }
    importInto(items, user, side, errors);
}

dw::xfer::ImportResult MainWindow::importInto(const QVector<dw::TransferItem> &items, int user, int side,
                                              const QVector<dw::xfer::ImportError> &readErrors) {
    side = qBound(0, side, qMax(0, sides() - 1));
    const dw::fs::Path dir = user >= 0 ? dw::fs::Path({Cpm::CpmVolume::userName(user)}) : dw::fs::Path();
    return importItems(side, dir, items, {}, readErrors);
}

// Host file names with letters plain ASCII can't hold (lønseddel.l): offer to choose the national character set for this
// disc first, so the letters are written the way that country's machines wrote them. False if the user cancelled.
static void hostNames(const QVector<dw::TransferItem> &items, QStringList *out) {
    for (const dw::TransferItem &x : items) {
        if (x.isDir) hostNames(x.kids, out);
        else if (x.sourceFs.isEmpty()) *out << x.name;
    }
}

bool MainWindow::askNatForNames(const QVector<dw::TransferItem> &items) {
    if (m_natId != "ascii" || autoAnswer || sides() < 1) return true;
    QStringList names;
    hostNames(items, &names);
    const QString letters = Nat::foreignLetters(names);
    if (letters.isEmpty()) return true;
    const QString shown = QStringList(letters.split("", Qt::SkipEmptyParts)).join(" ");
    QMessageBox box(QMessageBox::Question, "Character set",
                    QString("These file names have letters (%1) that a plain ASCII disc cannot hold, so they would be "
                            "replaced with -.\n\nDiscs from machines sold in some countries wrote such letters using the "
                            "characters [ \\ ] { | }. Choose the character set for this disc and they will be written that way.")
                        .arg(shown), QMessageBox::NoButton, this);
    QPushButton *choose = box.addButton("Choose character set...", QMessageBox::AcceptRole);
    QPushButton *plain = box.addButton("Plain ASCII", QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(choose);
    box.exec();
    if (box.clickedButton() == plain) return true;
    if (box.clickedButton() != choose) return false;
    QVector<const Cpm::Volume *> vols;
    for (int s = 0; s < sides(); ++s) vols << &vol(s);
    Nat::Dialog d(vols, m_natId, this, names);
    d.setLowerNames(m_lowerOk);
    if (d.exec() != QDialog::Accepted) return false;
    setNat(d.chosen(), true);
    return true;
}

static void withoutClashSuffixesAll(QVector<dw::TransferItem> *items) {      // each folder's list on its own
    RetroFiles::withoutClashSuffixes(items);
    for (dw::TransferItem &x : *items) if (x.isDir) withoutClashSuffixesAll(&x.kids);
}

// The import session's hooks: CP/M Disk Wrangler's own rules where they are not cpmdw's (the volume's placeIncoming).
// The drop target's user area wins over the user an item brings with it; a name made twice in one import is asked
// about, as a file that exists; a file exists only under exactly its name (the directory's bytes, as Store::indexOf
// compares them); a replacing file has the attributes it brings (none, from the host); host folders
// userN are user areas, other folders are flattened; names are made valid with this window's character set and case
// rule, and each one made valid is listed in the summary.
dw::ui::ImportHooks MainWindow::importHooks(int) {
    m_importRenamed.clear();                   // asked once per import, before anything is placed
    dw::ui::ImportHooks h;
    h.options.renameRepeats = false;
    h.options.exactNames = true;
    h.prepare = withoutClashSuffixesAll;
    h.beforeRun = [this](const QVector<dw::TransferItem> &items) { return askNatForNames(items); };
    h.dirHook = [this](const dw::TransferItem &folder, const dw::fs::Path &) {
        dw::xfer::DirChoice c;                 // Flatten, unless it is a user area this disc has
        int u = RetroFiles::userFromFolderName(folder.name);
        if (u > maxUser()) u = -1;
        if (u >= 0) {
            c.action = dw::xfer::DirChoice::Into;
            c.dir = dw::fs::Path({Cpm::CpmVolume::userName(u)});
        }
        return c;
    };
    h.place = [this](const dw::TransferItem &item, const dw::fs::Path &target) { return placeIncoming(item, target); };
    h.attrs = [](const dw::TransferItem &item, const dw::fs::Entry *) {
        const XItem x = RetroFiles::fromTransfer(item);      // the attributes the window reads from each source
        dw::fs::AttrValues a{{"readonly", x.readOnly}, {"system", x.system}, {"archive", x.archive}};
        for (int k = 0; k < 4; ++k) a[QString("f%1").arg(k + 1)] = bool(x.interfaceAttrs & (1 << k));
        return a;
    };
    return h;
}

dw::fs::Placement MainWindow::placeIncoming(const dw::TransferItem &item, const dw::fs::Path &target) {
    const XItem x = RetroFiles::fromTransfer(item);          // the user and name the window reads from each source
    int user = target.isRoot() ? (x.user >= 0 ? x.user : 0) : Cpm::CpmVolume::userOf(target.parts.value(0));
    if (user > maxUser() || user < 0) user = 0;
    // A name from another CP/M disc is kept exactly (its [ \ ] | were letters there); only a host file name is read as
    // Unicode and put into this disc's national set. Names from other filesystems are bytes: no set applies.
    const bool fromCpm = x.srcFs == "cpm", fromHost = x.srcFs.isEmpty();
    const QString extra = fromCpm ? QStringLiteral("[\\]|") + Nat::lowerLetters() : nameRules();
    const QString want = fromHost ? toDiscName(x.name) : fromCpm ? x.name : x.name.toUpper();
    QString why, name = want;
    if (!Cpm::Volume::validName(name, &why, extra)) {
        name = Cpm::Volume::toCpmName(fromHost ? want : x.name, extra);
        m_importRenamed << QString("%1 -> %2").arg(x.name, name);
    }
    if (!extra.contains('a')) name = name.toUpper();          // as the disc's own rules write it
    dw::fs::Placement p;
    p.dir = dw::fs::Path({Cpm::CpmVolume::userName(user)});
    p.name = name;
    return p;
}

dw::xfer::Conflict MainWindow::askReplace(int side, const dw::fs::Path &target, const dw::fs::Entry &, const dw::TransferItem &) {
    QMessageBox box(QMessageBox::Question, "File exists",
                    QString("%1 already exists in user %2%3. Replace it?").arg(target.name()).arg(Cpm::CpmVolume::userOf(target.parts.value(0)))
                        .arg(sides() > 1 ? QString(" on side %1").arg(side) : QString()),
                    QMessageBox::NoButton, this);
    QPushButton *rep = box.addButton("Replace", QMessageBox::AcceptRole);
    QPushButton *repAll = box.addButton("Replace all", QMessageBox::AcceptRole);
    QPushButton *skip = box.addButton("Skip", QMessageBox::RejectRole);
    QPushButton *skipAll = box.addButton("Skip all", QMessageBox::RejectRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == rep) return dw::xfer::Conflict::Replace;
    if (box.clickedButton() == repAll) return dw::xfer::Conflict::ReplaceAll;
    if (box.clickedButton() == skip) return dw::xfer::Conflict::Skip;
    if (box.clickedButton() == skipAll) return dw::xfer::Conflict::SkipAll;
    return dw::xfer::Conflict::Cancel;
}

void MainWindow::importSummary(const dw::xfer::ImportResult &r) {
    const int files = r.added + r.replaced;
    QString s = QString("Added %1 file%2").arg(files).arg(files == 1 ? "" : "s");
    if (r.replaced) s += QString(" (%1 replaced)").arg(r.replaced);
    if (r.skipped) s += QString(", skipped %1").arg(r.skipped);
    if (r.flattened) s += QString(", %1 folder%2 flattened (CP/M has no folders)").arg(r.flattened).arg(r.flattened == 1 ? "" : "s");
    if (!m_importRenamed.isEmpty()) s += QString(", %1 renamed").arg(m_importRenamed.size());
    m_status->setText(s + ".");
    QStringList detail;
    for (const dw::xfer::ImportError &e : r.errors) detail << e.why;
    if (!m_importRenamed.isEmpty() && m_importRenamed.size() <= 20) detail << "" << "Renamed to CP/M 8.3 names:" << m_importRenamed;
    if (!r.errors.isEmpty()) message("Add files", s + ".\n\n" + detail.join("\n"));
}

void MainWindow::addFilesDialog() {
    QStringList p = QFileDialog::getOpenFileNames(this, "Add files", AppSettings().value("addDir").toString());
    if (p.isEmpty()) return;
    AppSettings().setValue("addDir", QFileInfo(p.first()).absolutePath());
    QList<QTreeWidgetItem *> sel = tree()->selectedItems();
    importPaths(p, sel.isEmpty() ? -1 : targetUser(sel.first()), sel.isEmpty() ? 0 : targetSide(sel.first()));
}

// ------------------------------------------------------------------------------------------- copy and paste

// Edit > Copy takes a copy of the selected files there and then, so a paste copies what was selected at the time even
// if the disc has changed or its window has closed since. Inside this program a paste uses that copy (the clipboard
// carries a token naming it); for the desktop and other programs the clipboard offers the files, as a drag does
// (text/uri-list, the retro-files JSON and GNOME's list), written to a temporary folder only when something asks.
static QVector<dw::TransferItem> &clipItems() { static QVector<dw::TransferItem> v; return v; }
static QByteArray &clipToken() { static QByteArray t; return t; }

void MainWindow::copySelected() {
    const QVector<Item> chosen = selectedFiles();
    if (chosen.isEmpty()) { m_status->setText("Select the files to copy."); return; }
    QVector<dw::TransferItem> items;
    QString err;
    if (!exportForCopy(chosen, &items, &err)) { message("Copy", err); return; }
    static int serial = 0;
    clipItems() = items;
    clipToken() = QByteArray::number(qint64(QCoreApplication::applicationPid())) + ":" + QByteArray::number(++serial);
    auto *m = new dw::DragOutMime([items](QVector<dw::TransferItem> *out, QString *) { *out = items; return true; },
                                  RetroFiles::dragSpec(trimTextOnExtract(), QFileInfo(image().path()).fileName()));
    m->setForClipboard(true);
    m->setData(DW_MIME_CLIP, clipToken());
    QApplication::clipboard()->setMimeData(m);
    static bool once = false;                  // the copied files live in a folder removed at exit: don't leave
    if (!once) {                               // a clipboard behind that names them
        once = true;
        connect(qApp, &QCoreApplication::aboutToQuit, [] {
            const QMimeData *c = QApplication::clipboard()->mimeData();
            if (c && c->hasFormat(DW_MIME_CLIP) && QApplication::clipboard()->ownsClipboard()) QApplication::clipboard()->clear();
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
    return m && (m->hasFormat(DW_MIME_CLIP) || m->hasFormat(RetroFiles::MimeType) || m->hasUrls() ||
                 m->hasFormat("x-special/gnome-copied-files"));
}

// Edit > Paste: to the user number and side of the selection, as Add files does (no selection: each file keeps its
// own user number).
void MainWindow::paste() {
    if (!sides()) return;
    if (!writable()) { message("Paste", "The disc is read-only: " + readOnlyReason()); return; }
    QList<QTreeWidgetItem *> sel = tree()->selectedItems();
    int user = sel.isEmpty() ? -1 : targetUser(sel.first()), side = sel.isEmpty() ? 0 : targetSide(sel.first());
    const QMimeData *m = QApplication::clipboard()->mimeData();
    if (!m || !pasteItems(m, user, side))
        m_status->setText("Nothing to paste: copy some files first (here, in another window, or in a file manager).");
}

bool MainWindow::pasteItems(const QMimeData *m, int user, int side) {
    if (m->hasFormat(DW_MIME_CLIP) && !clipToken().isEmpty() && m->data(DW_MIME_CLIP) == clipToken()) {
        importInto(clipItems(), user, side);               // copied in this program: nothing written to the host
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
    QVector<Item> items;
    for (const FileRef &r : refs) items << itemOf(r);
    return DiscWindow::deleteFiles(items, err);
}

QString MainWindow::deleteQuestion(const QVector<Item> &items) const {
    return items.size() == 1 ? QString("Delete %1:%2?").arg(Cpm::CpmVolume::userOf(items[0].path.parts.value(0))).arg(items[0].path.name())
                             : QString("Delete %1 files?").arg(items.size());
}

bool MainWindow::canRemove(const Item &item, QString *err) const {        // a read-only file stays
    const Cpm::Volume &v = vol(item.mount);
    const int i = v.indexOf(Cpm::CpmVolume::userOf(item.path.parts.value(0)), item.path.name());
    if (i >= 0 && (v.files()[i].attrs & Cpm::AttrRO)) { *err = item.path.name() + " is read-only"; return false; }
    return true;
}

// Rename follows Delete: one change inside one Disc::transact, put back if it fails (the window keeps no snapshot).
void MainWindow::renameSelected() {
    const QVector<Item> items = selectedFiles();
    if (items.size() != 1) return;
    const Item &item = items[0];
    const FileRef r = refOf(item);
    bool ok;
    QString n = QInputDialog::getText(this, "Rename", QString("New name for %1 (8.3):").arg(toHostName(r.name)), QLineEdit::Normal,
                                      toHostName(r.name), &ok);
    if (!ok || n.trimmed().isEmpty()) return;
    const QString newName = m_natId != "ascii" ? toDiscName(n.trimmed()) : n.trimmed();
    QString err;
    bool done = false;
    if (!writable(item.mount)) err = "read-only: " + readOnlyReason(item.mount);
    else done = m_doc.disc().transact([&](QString *e) {
        dw::fs::Volume *v = m_doc.disc().volume(item.mount);
        if (!v) { *e = "no such volume"; return false; }
        return v->rename(item.path, item.path.parent().child(newName), e);
    }, &err);
    if (!done) message("Rename", err);
    refresh();
}

// Properties: the dialog is this program's, the change the kit's (DiscWindow::editProperties applies what this returns
// in one Disc::transact and refreshes). Per file: the attribute bits that differ, then a rename (a new name or user).
bool MainWindow::propertiesEditor(const QVector<Item> &items, PropertiesEdit *edit) {
    QVector<FileRef> refs;
    for (const Item &i : items) refs << refOf(i);
    auto fileAt = [this](const FileRef &r) -> const Cpm::File & {
        const Cpm::Store *st = m_doc.store(r.side);
        return st->files()[st->indexOf(r.user, r.name)];
    };
    QDialog d(this);
    d.setWindowTitle(refs.size() == 1 ? "Properties - " + toHostName(refs[0].name) : QString("Properties - %1 files").arg(refs.size()));
    auto *fl = new QFormLayout;
    const Cpm::File *one = refs.size() == 1 ? &fileAt(refs[0]) : nullptr;
    QLineEdit *name = nullptr;
    if (one) {
        name = new QLineEdit(toHostName(one->name));
        fl->addRow("Name", name);
        const dw::fs::Volume *v = m_doc.disc().volume(refs[0].side);
        const QString unit = v && v->fsId() == "torch" ? "sector" : "block";   // Torch: index sectors included
        fl->addRow("Size", new QLabel(QString("%1 bytes (%2 records%3), %4 %8%5, %6 directory entr%7")
                                          .arg(one->size).arg(one->records).arg(one->exactSize ? ", exact size recorded" : "")
                                          .arg(one->blocks).arg(one->blocks == 1 ? "" : "s").arg(one->entries.size())
                                          .arg(one->entries.size() == 1 ? "y" : "ies").arg(unit)));
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
        for (const FileRef &r : refs) if (fileAt(r).attrs & b.bit) ++on;
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
    if (d.exec() != QDialog::Accepted || !writable()) return false;
    for (const FileRef &r : refs) {
        const Cpm::Store *st = m_doc.store(r.side);
        const int i = st->indexOf(r.user, r.name);
        if (i < 0) continue;
        const quint16 was = st->files()[i].attrs;
        quint16 a = was;
        for (const Box &b : boxes) {
            if (b.cb->checkState() == Qt::Checked) a |= b.bit;
            else if (b.cb->checkState() == Qt::Unchecked) a &= quint16(~b.bit);
        }
        PropertyChange c;
        c.item = itemOf(r);
        if (a != was) c.attrs = Cpm::CpmVolume::attrValues(a);
        const QString newName = name ? toDiscName(name->text().trimmed()) : r.name;
        const int newUser = user->value() >= 0 ? user->value() : r.user;
        if (newName != r.name || newUser != r.user) c.to = itemOf(FileRef(r.side, newUser, newName)).path;
        edit->changes << c;
    }
    return true;
}

void MainWindow::setLabelDialog() { editVolume(currentSide(), "label"); }

// The disc label: this program's question, the kit's change (DiscWindow::editVolume).
bool MainWindow::volumeEditor(int side, const QString &, VolumeEdit *edit) {
    const Cpm::Store *st = m_doc.store(side);
    bool ok;
    QString n = QInputDialog::getText(this, "Disc label", "Label (CP/M 3, 8.3 characters):", QLineEdit::Normal,
                                      st ? st->label().name : QString(), &ok);
    if (!ok) return false;
    edit->values = {{"label", n}};
    edit->title = "Disc label";
    return true;
}

// ------------------------------------------------------------------------------------------- tools

// Check disc: the kit runs every side's check (DiscWindow::checkDisc); this program words the report, side by side.
QString MainWindow::checkText(const dw::fs::Report &, const QVector<dw::fs::Report> &perSide) const {
    QString s;
    for (int side = 0; side < perSide.size(); ++side) {
        const dw::fs::Report &r = perSide[side];
        const Cpm::Store *st = m_doc.store(side);
        const dw::fs::Volume *v = m_doc.disc().volume(side);
        if (perSide.size() > 1) s += QString("Side %1:\n").arg(side);
        if (r.ok() && r.warnings.isEmpty()) s += "No problems found.\n";
        if (!r.errors.isEmpty()) s += "Errors:\n" + r.errors.join("\n") + "\n";
        if (!r.warnings.isEmpty()) s += "Warnings:\n" + r.warnings.join("\n") + "\n";
        if (v->fsId() == "torch")
            s += QString("%1 files; %2 of %3 directory entries free; %4 of %5 sectors free (Torch CP/N).\n\n")
                     .arg(st->files().size()).arg(st->freeEntries()).arg(Torch::Entries)
                     .arg(st->freeBlocks()).arg(Torch::Sectors - Torch::Reserved);
        else
            s += QString("%1 files in %2 directory entries; %3 of %4 entries free; %5 of %6 blocks free.\n\n")
                     .arg(st->files().size()).arg(st->usedEntries()).arg(st->freeEntries()).arg(st->format().maxdir)
                     .arg(st->freeBlocks()).arg(st->format().dsm + 1);
    }
    return s.trimmed();
}

void MainWindow::discInfo() {
    QStringList l;
    dw::device::Shape sh = image().shape();
    l << "<h3>" + QFileInfo(image().path()).fileName().toHtmlEscaped() + "</h3>";
    l << "<b>Container:</b> " + dw::device::kindName(image().kind());
    if (!image().headerLine().isEmpty()) l << "<b>ImageDisk header:</b> " + image().headerLine().toHtmlEscaped();
    if (!image().comment().isEmpty()) l << "<b>Comment / creator:</b> " + image().comment().toHtmlEscaped().replace("\n", "<br>");
    l << QString("<b>Physical:</b> %1 cylinders, %2 side(s); %3 tracks of %4 x %5 bytes, sector IDs from %6")
             .arg(image().cylinders()).arg(image().heads()).arg(sh.uniformTracks).arg(sh.sectrk).arg(sh.seclen).arg(sh.firstId);
    if (sh.oddTracks) l << QString("&nbsp;&nbsp;%1 track(s) of another shape (often a single-density boot track)").arg(sh.oddTracks);
    if (sh.badSectors) l << QString("&nbsp;&nbsp;%1 bad, missing or duplicated sector(s)").arg(sh.badSectors);
    if (image().kind() == dw::device::Kind::Raw) l << QString("<b>Raw file size:</b> %1 bytes").arg(image().rawFileSize());
    if (sides()) {
        const Cpm::Format &f = vol(0).format();
        l << "<b>Format:</b> " + f.displayName().toHtmlEscaped() + " (" + f.origin.toHtmlEscaped() + ")";
        l << "&nbsp;&nbsp;" + f.summary().toHtmlEscaped();
        if (!f.source.isEmpty()) l << "&nbsp;&nbsp;Source: " + f.source.toHtmlEscaped();
        if (f.torch) {
            const Cpm::Volume &v = vol(0);
            l << QString("<b>Filesystem:</b> Torch CP/N - 256 directory entries of 16 bytes in track 0 and track 1 sectors 0-5, "
                         "an allocation map in track 1 sectors 6-7; each file reached through index sectors (one L3 sector "
                         "for up to 128 data sectors, an L2 sector above more), two 128-byte records to a sector");
            l << QString("<b>Directory:</b> %1 of %2 entries used; %3 of %4 sectors free")
                     .arg(v.usedEntries()).arg(Torch::Entries).arg(v.freeBlocks()).arg(Torch::Sectors - Torch::Reserved);
            l << QString("<b>Detection:</b> confidence %1. %2").arg(m_det.confidence, detectionText(m_det).toHtmlEscaped());
            l << "<b>As a definition:</b><pre>" + f.toDiskdef().toHtmlEscaped() + "</pre>";
        } else {
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
            for (int side = 0; side < sides(); ++side) {
                const Cpm::Volume &v = vol(side);
                l << QString("<b>Directory%5:</b> %1 of %2 entries used; %3; label %4")
                         .arg(v.usedEntries()).arg(f.maxdir).arg(v.hasStamps() ? "time stamps: " + v.stampKinds() : QString("no time stamps"))
                         .arg(v.label().present ? "'" + v.label().name.toHtmlEscaped() + "'" : QString("none"))
                         .arg(sides() > 1 ? QString(" (side %1)").arg(side) : QString());
            }
            l << QString("<b>Detection:</b> confidence %1. %2").arg(m_det.confidence, detectionText(m_det).toHtmlEscaped());
            l << "<b>As a cpmtools definition:</b><pre>" + f.toDiskdef().toHtmlEscaped() + "</pre>";
        }
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
    dw::device::RawLayout before = image().rawLayout();
    QString e;
    for (Detect::Candidate &c : m_det.ranked) {
        if (image().kind() == dw::device::Kind::Raw && c.raw.valid() && !image().setRawLayout(c.raw, &e)) continue;
        c = Detect::reevaluate(image(), c);
    }
    if (image().kind() == dw::device::Kind::Raw && before.valid()) image().setRawLayout(before, &e);
}

void MainWindow::formatDialog() {
    if (!image().isOpen()) return;
    if (image().hasPending()) { message("Disc format", "Save or revert your changes before reading the disc with another format."); return; }
    dw::device::RawLayout before = image().rawLayout();
    rescoreReadings();
    FormatDialog d(&image(), m_det, m_detIndex, m_doc.safe(), this);
    int r = d.exec();
    QString err;
    if (image().kind() == dw::device::Kind::Raw && before.valid()) image().setRawLayout(before, &err);   // previews may change it
    if (r != QDialog::Accepted) { for (int side = 0; side < sides(); ++side) vol(side).reload(); return; }
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
                       "standard and extended DSK (the format CPCEMU introduced), and TeleDisk (.TD0, read-only). Works out each disc's format from its contents, "
                       "and exchanges files with Acorn Disk Wrangler and other tools by drag and drop (retro-files format).<br><br>"
                       "Copyright &copy; 2026 Daniel Jameson.<br>Licensed under the GNU General Public License v3 or later.<br><br>"
                       "Includes the disc definitions from cpmtools 2.23 (Michael Haardt and contributors, GPL v3 or later). "
                       "The way the format detection ranks rival readings by what their files contain is based on an idea in AutoArchaeologist (Datamuseum-DK, BSD 2-Clause). "
                       "The TeleDisk decoder follows LZHUF by Haruyasu Yoshizaki and Haruhiko Okumura. Uses Qt under the LGPL v3. "
                       "Help &gt; Licences shows the full licence texts and notices, including those of the libraries distributed with it.<br><br>" +
                           AppSettings::where().toHtmlEscaped());
}
