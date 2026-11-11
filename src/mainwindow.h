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
#include <QCheckBox>
#include <QToolButton>

#include <QDateTime>
#include <QMainWindow>
#include <QPair>
#include <QTreeWidget>
#include <memory>

#include <diskwrangler/ui/discwindow.h>

#include "cpmfs.h"
#include "cpmvolume.h"
#include "detect.h"
#include "diskimage.h"          // dw::device::Container, and the Disk:: names guitest still uses
#include "natchars.h"
#include "dragout.h"
#include "mainwindow_p.h"

class QAction;
class QLabel;
class QMimeData;

// A file on the disc: which side's filesystem (0 unless the disc has a separate one on each side), user, name.
// (The window kit's DiscWindow::Item is the same as (mount = side, Path {"user N", name}); FileRef stays for the code
// and the tests that use it.)
struct FileRef {
    int side = 0, user = 0;
    QString name;
    FileRef() = default;
    FileRef(int s, int u, const QString &n) : side(s), user(u), name(n) {}
    bool operator==(const FileRef &o) const { return side == o.side && user == o.user && name == o.name; }
};

// The file tree: the window kit's pane, which also accepts files and folders from the desktop, items from other
// windows of this program and retro-files drags from other programs, and drags its selection out the same three ways
// (the drags out are made from the kit's exportItems since step 5d; what is dropped goes to the kit's import session
// since step 5e).
class DropTree : public dw::ui::VolumePane {
    Q_OBJECT
public:
    explicit DropTree(QWidget *parent = nullptr);
    void setOwner(quint64 owner) { m_owner = owner; }
    QMimeData *makeMime(const QList<QTreeWidgetItem *> &items) const;
    static bool fromThisProcess(const QMimeData *m);
signals:
    void pathsDropped(const QStringList &paths, QTreeWidgetItem *target);
    void internalDrop(const QByteArray &payload, QTreeWidgetItem *target);
    void retroFilesDropped(const QByteArray &json, QTreeWidgetItem *target);
    void dragOutFinished(int files, const QStringList &errors);
protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    QStringList mimeTypes() const override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)               // Qt 6 passes the list by reference, Qt 5 by value
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
#else
    QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override;
#endif
    Qt::DropActions supportedDropActions() const override;
    void startDrag(Qt::DropActions supported) override;
private:
    bool acceptable(const QMimeData *m) const;
    mutable std::shared_ptr<RetroFiles::DragState> m_lastDrag;
    quint64 m_owner = 0;
};

// The main window: DiskWranglerCore's DiscWindow (step 5c) over a Cpm::OpenedDisc. The kit does the rows, the
// selection, the title, Save / Save As / Revert / Close, Delete, Extract, every import (step 5e: the import session,
// with CP/M's questions and rules as its hooks), Properties, Disc label and Check (step 5f) and opening (step 5g: the
// open pipeline, with CP/M's detection, its "not CP/M" message and the format dialog as its hooks); this class fills
// its hooks and keeps the rest of CP/M Disk Wrangler's window (the format, character sets, the Tools dialogs) until
// 5h moves them onto the kit.
class MainWindow : public dw::ui::DiscWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    static MainWindow *byId(quint64 id);
    static QList<MainWindow *> windows();                // every main window, oldest first (the kit's DiscWindow::all())

    dw::app::Document *document() const override { return const_cast<Cpm::OpenedDisc *>(&m_doc); }
    dw::app::AppInfo appInfo() const override;

    // ----- for the menus, and for tests
    void setLowerNames(bool on, bool remember);       // allow lower-case file names on this disc
    bool lowerNames() const { return m_lowerOk; }
    void setNat(const QString &id, bool remember);     // the disc's national character set (natchars.h)
    QString natId() const { return m_natId; }
    QString natHint() const { return m_natHint; }
    bool openImage(const QString &path);                // the kit's openFile (the open pipeline)
    bool newImage(const QString &path, dw::device::Kind kind, const Cpm::Format &f, bool stamps, const QString &label);
    bool useFormat(const Detect::Candidate &c, bool allowChanges);     // re-read with another format
    // user -1: each file's own user number (0 for files without one); side: which side's filesystem
    void importPaths(const QStringList &paths, int user, int side = 0);
    bool importRetroFiles(const QByteArray &json, int user, int side = 0);
    // The kit's import (importItems) into a user area (-1: each file's own user) of a side: what every import ends in.
    dw::xfer::ImportResult importInto(const QVector<dw::TransferItem> &items, int user, int side,
                                      const QVector<dw::xfer::ImportError> &readErrors = {});
    // Copying out (View, Copy, a drag, a drop on another window): the kit's exportItems, as CP/M's copy-out wants it.
    bool exportForCopy(const QVector<Item> &items, QVector<dw::TransferItem> *out, QString *err) const;
    QVector<FileRef> selectedRefs() const;
    static Item itemOf(const FileRef &r) { return Item(r.side, dw::fs::Path({Cpm::CpmVolume::userName(r.user), r.name})); }
    static FileRef refOf(const Item &i) { return FileRef(i.mount, Cpm::CpmVolume::userOf(i.path.parts.value(0)), i.path.name()); }
    bool deleteFiles(const QVector<FileRef> &refs, QString *err);     // the kit's deleteFiles, by FileRef
    using DiscWindow::deleteFiles;

    dw::device::Container &image() { return m_doc.container(); }
    const dw::device::Container &image() const { return m_doc.container(); }
    Cpm::Volume &volume(int side = 0) { return m_doc.legacy(side); }
    int sides() const { return m_doc.sides(); }          // filesystems shown: 1, or 2 for a disc on each side
    bool dropOpensImage() const { return sides() == 0; }   // nothing read as CP/M here: a drop opens an image
    const Detect::Result &detection() const { return m_det; }
    // Scores the readings again on the disc as it is now (done before Tools > Disc format shows them).
    void rescoreReadings();
    DropTree *tree() const { return static_cast<DropTree *>(pane()); }
    bool writable() const { return DiscWindow::writable(0); }
    QString readOnlyReason() const { return DiscWindow::readOnlyReason(0); }
    using DiscWindow::writable;
    using DiscWindow::readOnlyReason;
    bool trimTextOnExtract() const;

    // ----- the kit's slots this program fills (until later series of step 5 make them the kit's)
    void newWindow() override;
    void newImageDialog() override;
    void openDialog() override;
    void viewSelected() override;
    void addFilesDialog() override;
    void copySelected() override;
    void paste() override;
    void renameSelected() override;
    void about() override;

protected:
    // ----- the kit's hooks
    ActionSpec actionSpec(Act a) const override;
    QStringList paneGroups() const override { return {"cpm"}; }
    dw::ui::PaneLayout paneLayout(const QString &group) const override;
    dw::ui::VolumePane *createPane(const QString &group) override;
    QList<QAction *> contextActions() const override;
    QString rootLabel(int mount) const override;
    void decorateRow(QTreeWidgetItem *row, int mount, const dw::fs::Path &path, const dw::fs::Entry *entry) const override;
    void filesUnder(int mount, const dw::fs::Path &dir, QVector<dw::fs::Path> *out) const override;
    QStringList statusVariants() const override;
    void presentStatus(const QStringList &variants) override;
    void showMessage(const QString &text) override;
    void updateActions() override;
    bool keepBackup() const override;
    bool beforeSave() override;
    void afterSave() override;
    bool askSaveAs(QString *path, dw::device::Kind *kind) override;
    bool afterSaveAs(const QString &path, QString *err) override;
    void revertDocument() override;
    QString deleteQuestion(const QVector<Item> &items) const override;
    bool canRemove(const Item &item, QString *err) const override;
    std::shared_ptr<dw::xfer::HostPolicy> hostPolicy(const QVector<dw::TransferItem> &items) const override;
    dw::ui::ImportHooks importHooks(int side) override;
    dw::xfer::Conflict askReplace(int side, const dw::fs::Path &target, const dw::fs::Entry &existing,
                                  const dw::TransferItem &incoming) override;
    void importSummary(const dw::xfer::ImportResult &r) override;
    bool propertiesEditor(const QVector<Item> &items, PropertiesEdit *edit) override;
    bool volumeEditor(int side, const QString &key, VolumeEdit *edit) override;
    QString checkText(const dw::fs::Report &all, const QVector<dw::fs::Report> &perSide) const override;
    bool detectImage(const QString &path, int force, dw::fs::Verdict *verdict) override;
    bool openDocument(const QString &path, const dw::fs::Verdict &verdict, const dw::fs::Candidate *pick, int force) override;
    void openStopped() override;
    NotOpened handOverMessage(const QString &path, const dw::fs::Verdict &verdict, const dw::fs::Candidate &pick) const override;
    void afterOpen(const QString &path) override;
    dw::ui::DiscWindow *createWindow() override;
    bool closeFailedWindow() const override { return false; }     // a new window that didn't open the disc stays open

private slots:
    void setLabelDialog();
    void discInfo();
    void formatDialog();
    void addDiskdefsDialog();
    void onPathsDropped(const QStringList &paths, QTreeWidgetItem *target);
    void onInternalDrop(const QByteArray &payload, QTreeWidgetItem *target);
    void onRetroFilesDropped(const QByteArray &json, QTreeWidgetItem *target);

private:
    const Cpm::File *fileOf(int side, int user, const QString &name) const;   // the side's file of that user and name
    bool attachFormat(const Detect::Candidate &c, QString *err);
    NotOpened notOpened(const QString &path) const;    // "<file>, how it was read, why it isn't CP/M" with Choose a format...
    // The disc's national character set (natchars.h): how names and text are shown, extracted, added and viewed.
    Nat::Set nat() const { return Nat::find(m_natId); }
    QString rememberedNat() const;
    void natDialog();
    bool m_lowerOk = false;            // Tools > Allow lower-case file names: ticked for a disc that has any (or as chosen)
    bool lowerDisc() const { return m_lowerOk; }
    void showLower(bool on);
    void refreshNamesButton();
    void autoLower();                  // set it from what the disc holds, or from what was chosen for this disc before
    QVector<const Cpm::Volume *> volumePtrs() const;
    QString nameRules() const { return Nat::extraNameChars(nat()) + (lowerDisc() ? Nat::lowerLetters() : QString()); }
    void applyNameRules();             // the character set and case rule, to every side's volume
    bool askNatForNames(const QVector<dw::TransferItem> &items);
    QString m_natId = "ascii";
    QString m_natHint;                 // shown in the status bar: this disc looks as if it uses a national character set
    QString toHostName(const QString &discName) const { return Nat::toHost(discName, nat()); }
    QString toDiscName(const QString &hostName) const { return Nat::fromHost(hostName, nat(), !lowerDisc()); }   // case as the disc keeps it
    void rememberFormat();
    QString rememberedFormat() const;
    int targetUser(QTreeWidgetItem *it) const;
    int targetSide(QTreeWidgetItem *it) const;
    int currentSide() const;                            // the side of the selection (0 if none)
    int maxUser() const;
    void detachAll();
    dw::fs::Placement placeIncoming(const dw::TransferItem &item, const dw::fs::Path &target);
    QStringList m_importRenamed;       // "name -> NAME" for each name made valid in the current import (its summary lists them)
    bool clipboardUsable() const;                       // something Paste can add
    bool pasteItems(const QMimeData *m, int user, int side);
    Cpm::Volume &vol(int side) const { return const_cast<Cpm::OpenedDisc &>(m_doc).legacy(side); }

    Cpm::OpenedDisc m_doc;
    Detect::Result m_det;
    int m_detIndex = 0;                // which ranked reading is in use (-1: one set by hand)
    Detect::Candidate m_current;
    StatusLabel *m_status = nullptr;
    QToolButton *m_natBtn = nullptr;   // right of the status line: "Names: <character set>, <case>"; its menu has the lower-case option and the character set dialog
    QAction *m_actSave = nullptr, *m_actSaveAs = nullptr, *m_actRevert = nullptr, *m_actAdd = nullptr, *m_actDelete = nullptr,
        *m_actProps = nullptr, *m_actRename = nullptr, *m_actLabel = nullptr, *m_actExtract = nullptr, *m_actView = nullptr,
        *m_actCheck = nullptr, *m_actInfo = nullptr, *m_actFormat = nullptr, *m_actCopy = nullptr, *m_actPaste = nullptr;
    QAction *m_optTrim = nullptr, *m_optFolders = nullptr, *m_optBackup = nullptr, *m_actNat = nullptr, *m_actLower = nullptr;
};
