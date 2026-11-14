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
// natchars.h - national character sets. CP/M names and texts are 7-bit, and on machines sold in many countries the
// ASCII positions @ [ \ ] ^ ` { | } ~ held that country's letters instead (the ISO 646 national variants): Danish
// "brøkregning" is written br|kregning. Nothing on the disc says which set it uses, so the user chooses one per disc;
// it only changes how names and text are shown, extracted, added and viewed, never the bytes on the disc. # and $
// are never mapped (real names use them far more often than £ or ¤).
#pragma once
#include <QByteArray>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include "natsets.h"          // the sets and the name conversions (QtCore only: cpmdw uses them too)

class QListWidget;
class QPlainTextEdit;
class QLabel;
namespace Cpm { class Store; }

namespace Nat {


// Letters in host file names that plain ASCII cannot hold (ø, å, ü ...), upper-cased, each once; and the national set
// that has a place for all of them (the first in the list if several do; empty if none does).
QString foreignLetters(const QStringList &names);
QStringList candidatesFor(const QStringList &names);     // every set with a place for all of them, in list order
QString suggestFor(const QStringList &names);            // the first of those, or empty

struct Evidence {
    int nameFiles = 0;           // file names with [ \ ] { | } next to a letter
    int textHits = 0;            // such characters between letters in text files
    QStringList names;           // up to a dozen of those names, as on the disc
    QString sample;              // some text that uses them, as on the disc
    QString guess;               // the set that reads the sample best, or empty
    bool any() const { return nameFiles >= 1 || textHits >= 3; }
};
// Looks at the file names and the start of the text files of these volumes.
Evidence scan(const QVector<const Cpm::Store *> &vols);
// Which set's common words the text reads best with: its id, or empty if none stands out.
QString guessSet(const QString &discText);

// Tools > Character set for names and text: the sets, with a preview made from the disc's own names and text.
class Dialog : public QDialog {
    Q_OBJECT
public:
    // incoming: host file names about to be added; the dialog says which set their letters belong to, preselects it and
    // previews how each name would be written to the disc in the chosen set.
    Dialog(const QVector<const Cpm::Store *> &vols, const QString &current, QWidget *parent = nullptr,
           const QStringList &incoming = QStringList());
    QString chosen() const;
    void setLowerNames(bool on) { m_lower = on; render(); }   // the preview writes names in lower case                // the set's id
private slots:
    void render();
private:
    Evidence m_ev;
    QStringList m_incoming;
    QVector<const Cpm::Store *> m_vols;
    bool m_lower = false;
    QListWidget *m_list;
    QPlainTextEdit *m_preview;
    QLabel *m_note;
};

} // namespace Nat
