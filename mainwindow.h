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

#include <QDateTime>
#include <QMainWindow>
#include <QPair>
#include <QTreeWidget>
#include <memory>

#include "cpmfs.h"
#include "detect.h"
#include "diskimage.h"
#include "dragout.h"
#include "mainwindow_p.h"

class QAction;
class QLabel;
class QMimeData;

// A file on the disc: which side's filesystem (0 unless the disc has a separate one on each side), user, name.
struct FileRef {
    int side = 0, user = 0;
    QString name;
    FileRef() = default;
    FileRef(int s, int u, const QString &n) : side(s), user(u), name(n) {}
    bool operator==(const FileRef &o) const { return side == o.side && user == o.user && name == o.name; }
};

// The file tree: accepts files and folders from the desktop, items from other windows of this program and
// retro-files drags from other programs; drags its selection out the same three ways.
class DropTree : public QTreeWidget {
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

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    quint64 id() const { return quint64(quintptr(this)); }
    static MainWindow *byId(quint64 id);
    static QList<MainWindow *> &windows();

    // ----- for the menus, and for tests
    bool openImage(const QString &path);
    bool newImage(const QString &path, Disk::Kind kind, const Cpm::Format &f, bool stamps, const QString &label);
    bool save();
    bool saveAs(const QString &path, Disk::Kind kind);
    bool useFormat(const Detect::Candidate &c, bool allowChanges);     // re-read with another format
    // user -1: each file's own user number (0 for files without one); side: which side's filesystem
    void importPaths(const QStringList &paths, int user, int side = 0);
    void importItems(const QVector<XItem> &items, int user, int side, ImportCtx &c);
    bool importRetroFiles(const QByteArray &json, int user, int side = 0);
    bool exportFiles(const QVector<FileRef> &refs, QVector<XItem> *out, QString *err) const;
    QVector<FileRef> selectedRefs() const;
    bool deleteFiles(const QVector<FileRef> &refs, QString *err);
    void refresh();

    Disk::Image &image() { return m_img; }
    Cpm::Volume &volume(int side = 0) { return m_vols[side]; }
    int sides() const { return m_nvols; }
    bool dropOpensImage() const { return m_nvols == 0; }   // nothing read as CP/M here: a drop opens an image               // filesystems shown: 1, or 2 for a disc on each side
    const Detect::Result &detection() const { return m_det; }
    DropTree *tree() { return m_tree; }
    bool writable() const;
    QString readOnlyReason() const;
    bool trimTextOnExtract() const;
    static bool autoAnswer;            // tests: answer "replace all" to overwrite questions, no message boxes

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void openDialog();
    void newImageDialog();
    void saveAsDialog();
    void revert();
    void addFilesDialog();
    void deleteSelected();
    void editProperties();
    void renameSelected();
    void setLabelDialog();
    void extractSelected();
    void viewSelected();
    void checkDisc();
    void discInfo();
    void formatDialog();
    void addDiskdefsDialog();
    void about();
    void showContextMenu(const QPoint &pos);
    void onPathsDropped(const QStringList &paths, QTreeWidgetItem *target);
    void onInternalDrop(const QByteArray &payload, QTreeWidgetItem *target);
    void onRetroFilesDropped(const QByteArray &json, QTreeWidgetItem *target);
    void updateUi();

private:
    bool attachFormat(const Detect::Candidate &c, QString *err);
    void rememberFormat();
    QString rememberedFormat() const;
    int targetUser(QTreeWidgetItem *it) const;
    int targetSide(QTreeWidgetItem *it) const;
    int currentSide() const;                            // the side of the selection (0 if none)
    int maxUser() const;
    void detachAll();
    bool confirmDiscard();
    void importOne(const XItem &x, int user, int side, ImportCtx &c);
    void importSummary(const ImportCtx &c);
    void message(const QString &title, const QString &text, bool warning = true);
    QString title() const;

    Disk::Image m_img;
    Cpm::Volume m_vols[2];
    int m_nvols = 0;
    Detect::Result m_det;
    int m_detIndex = 0;                // which ranked reading is in use (-1: one set by hand)
    Detect::Candidate m_current;
    bool m_safe = false;               // detection or the user says the format can be written with
    bool m_checkOk = true;             // the directory passed check() when it was opened
    QDateTime m_fileTime;
    qint64 m_fileSize = -1;
    DropTree *m_tree;
    QLabel *m_status;
    QAction *m_actSave, *m_actSaveAs, *m_actRevert, *m_actAdd, *m_actDelete, *m_actProps, *m_actRename,
        *m_actLabel, *m_actExtract, *m_actView, *m_actCheck, *m_actInfo, *m_actFormat;
    QAction *m_optTrim, *m_optFolders, *m_optBackup;
};
