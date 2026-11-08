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
#include "natchars.h"
#include "cpmfs.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>

namespace Nat {

QString foreignLetters(const QStringList &names) {
    QString out;
    for (const QString &n : names)
        for (QChar c : n) {
            if (c.unicode() < 128 || !c.isLetter()) continue;
            const QChar u = c == QChar(0x00DF) ? c : c.toUpper();
            if (!out.contains(u)) out += u;
        }
    return out;
}

QStringList candidatesFor(const QStringList &names) {
    QStringList out;
    const QString letters = foreignLetters(names);
    if (letters.isEmpty()) return out;
    for (const Set &s : sets()) {
        if (s.id == "ascii") continue;
        bool all = true;
        for (QChar c : letters) if (!s.shown.contains(c) && !s.shown.contains(c.toLower())) { all = false; break; }
        if (all) out << s.id;
    }
    return out;
}

QString suggestFor(const QStringList &names) { return candidatesFor(names).value(0); }

// ------------------------------------------------------------------------------------------- evidence

static bool codeLike(const QByteArray &n11) {     // a source file or a listing: { | } mean what they mean in code
    static const QSet<QByteArray> exts = {"C  ", "H  ", "CPP", "PAS", "ASM", "MAC", "PRN", "LST", "Z80", "REL", "SYM",
                                          "COM", "EXE", "OVR", "SUB", "HEX", "BIN", "SYS", "LIB", "OBJ", "CMD"};
    return exts.contains(n11.mid(8, 3));
}

static bool plainText(const QByteArray &d) {      // 7-bit text: nothing but printable characters and layout
    int n = 0;
    for (char ch : d) {
        quint8 c = quint8(ch);
        if (c == 0x1A) break;
        if (c < 0x20 && c != '\r' && c != '\n' && c != '\t' && c != 0x0C) return false;
        if (c >= 0x7F) return false;
        ++n;
    }
    return n >= 32;
}

Evidence scan(const QVector<const Cpm::Volume *> &vols) {
    Evidence ev;
    static const QRegularExpression inName("[A-Za-z][\\[\\\\\\]{|}]|[\\[\\\\\\]{|}][A-Za-z]");
    static const QRegularExpression inLower("[a-z][{|}][a-z]");
    static const QRegularExpression inUpper("[A-Z][\\[\\\\\\]][A-Z]");
    QString allText;
    int textFiles = 0;
    for (const Cpm::Volume *v : vols) {
        if (!v) continue;
        for (const Cpm::File &f : v->files()) {
            if (inName.match(f.name).hasMatch()) {
                ++ev.nameFiles;
                if (ev.names.size() < 12) ev.names << f.name;
            }
            if (textFiles >= 40 || codeLike(f.name11) || f.size < 32) continue;
            QByteArray d;
            QString err;
            if (!v->readFile(f, &d, &err)) continue;
            d = d.left(8192);
            if (!plainText(d)) {                 // a tokenised program or data file: its text strings still help the guess
                QString runs, cur;
                for (char ch : d) {
                    const quint8 c = quint8(ch);
                    if (c >= 0x20 && c < 0x7F) cur += QChar(c);
                    else { if (cur.size() >= 5) runs += cur + "\n"; cur.clear(); }
                }
                if (cur.size() >= 5) runs += cur + "\n";
                if (!f.name11.mid(8, 3).isEmpty() && runs.size() > 40) allText += runs.left(4096) + "\n";
                continue;
            }
            ++textFiles;
            const QString t = QString::fromLatin1(d);
            int hits = 0;
            QRegularExpressionMatchIterator it = inLower.globalMatch(t);
            while (it.hasNext()) { it.next(); ++hits; }
            it = inUpper.globalMatch(t);
            while (it.hasNext()) { it.next(); ++hits; }
            if (hits) {
                ev.textHits += hits;
                if (ev.sample.size() < 1500) ev.sample += t.left(1500 - ev.sample.size()) + "\n";
            }
            allText += t.left(4096) + "\n";
        }
    }
    ev.guess = guessSet(allText);
    return ev;
}

QString guessSet(const QString &discText) {
    // A handful of very common words for each language, tried on the text read with each set.
    static const struct { const char *id; QStringList words; } lang[] = {
        {"dk-no", {"og", "ikke", "det", "som", "til", "med", "har", "jeg", "på", "af", "der", "hvad", "kan", "skal", "også", "når"}},
        {"se-fi", {"och", "inte", "det", "som", "för", "med", "har", "jag", "på", "av", "vad", "kan", "ska", "är", "men", "till"}},
        {"de",    {"und", "nicht", "der", "die", "das", "ist", "mit", "für", "ein", "eine", "auf", "wie", "sie", "ich", "zu"}},
        {"fr",    {"le", "la", "les", "des", "est", "pour", "que", "une", "dans", "pas", "vous", "du", "à", "et"}},
        {"it",    {"il", "di", "che", "per", "una", "non", "con", "sono", "della", "più", "è", "le", "gli"}},
        {"es",    {"el", "los", "las", "que", "por", "una", "con", "para", "del", "es", "no", "se"}},
        {"pt",    {"os", "as", "que", "para", "uma", "com", "não", "do", "da", "em", "é", "um"}},
    };
    if (discText.size() < 200) return QString();
    int best = 0, second = 0;
    QString bestId;
    for (const auto &l : lang) {
        const QString t = toHost(discText, find(QString::fromLatin1(l.id))).toLower();
        int hits = 0;
        for (const QString &w : l.words) {
            QRegularExpression re("(?<![\\p{L}])" + QRegularExpression::escape(w) + "(?![\\p{L}])");
            QRegularExpressionMatchIterator it = re.globalMatch(t);
            while (it.hasNext()) { it.next(); ++hits; }
        }
        if (hits > best) { second = best; best = hits; bestId = QString::fromLatin1(l.id); }
        else if (hits > second) second = hits;
    }
    return best >= 6 && best >= second * 3 / 2 + 2 ? bestId : QString();
}

// ------------------------------------------------------------------------------------------- the dialog

Dialog::Dialog(const QVector<const Cpm::Volume *> &vols, const QString &current, QWidget *parent,
               const QStringList &incoming) : QDialog(parent), m_incoming(incoming), m_vols(vols), m_lower(usesLowerCase(vols)) {
    setWindowTitle("Character set for names and text");
    resize(720, 480);
    m_ev = scan(vols);
    m_note = new QLabel;
    m_note->setWordWrap(true);
    m_list = new QListWidget;
    for (const Set &s : sets()) {
        auto *it = new QListWidgetItem(s.name, m_list);
        it->setData(Qt::UserRole, s.id);
        if (s.id == current) m_list->setCurrentItem(it);
    }
    if (!m_list->currentItem()) m_list->setCurrentRow(0);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);      // the list is as wide as its longest entry;
    m_list->setFixedWidth(m_list->sizeHintForColumn(0) + 2 * m_list->frameWidth() + 12);   // the preview takes the rest
    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);                 // long lines scroll, not wrap
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    QString note = "These discs are 7-bit: on machines sold in many countries the characters @ [ \\ ] ^ ` { | } ~ held that "
                   "country's letters. Nothing on the disc says which, so choose the one that reads right below. Only the "
                   "way names and text are shown, extracted, added and viewed changes; the disc is never altered.";
    if (m_ev.any()) {
        note += QString("\n\nThis disc uses those characters next to letters in %1 file name%2 and %3 place%4 in its text files")
                    .arg(m_ev.nameFiles).arg(m_ev.nameFiles == 1 ? "" : "s").arg(m_ev.textHits).arg(m_ev.textHits == 1 ? "" : "s");
        if (!m_ev.guess.isEmpty()) note += QString(", and its text reads best as %1.").arg(Nat::find(m_ev.guess).name.section("  (", 0, 0));
        else note += ".";
    } else if (m_incoming.isEmpty()) {
        note += "\n\nNothing on this disc clearly uses them.";
    }
    QString suggest;
    if (!m_incoming.isEmpty()) {
        const QString letters = QStringList(foreignLetters(m_incoming).split("", Qt::SkipEmptyParts)).join(" ");
        const QStringList cands = candidatesFor(m_incoming);
        QStringList nm;
        for (const QString &id : cands) nm << Nat::find(id).name.section("  (", 0, 0);
        QString why = QString("The files being added have the letter%1 %2, which plain ASCII has no place for. ")
                          .arg(letters.size() > 1 ? "s" : "", letters);
        if (nm.isEmpty()) why += "None of these sets has all of them, so some would still be replaced with -.";
        else if (nm.size() == 1) why += QString("That is %1: it is selected below.").arg(nm.first());
        else why += QString("Those are used by %1 (selected: %2); check the preview and choose the one that matches the disc.")
                        .arg(nm.join(" and "), nm.first());
        note = why + "\n\n" + note;
        suggest = cands.value(0);
    }
    m_note->setText(note);
    if (m_list->currentItem()->data(Qt::UserRole).toString() == "ascii" && !suggest.isEmpty())
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->data(Qt::UserRole).toString() == suggest) { m_list->setCurrentRow(i); break; }
    if (m_list->currentItem()->data(Qt::UserRole).toString() == "ascii" && !m_ev.guess.isEmpty())
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->data(Qt::UserRole).toString() == m_ev.guess) { m_list->setCurrentRow(i); break; }

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *row = new QHBoxLayout;
    row->addWidget(m_list);
    row->addWidget(m_preview, 1);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_note);
    lay->addLayout(row, 1);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, &Dialog::render);
    render();
}

QString Dialog::chosen() const {
    return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString("ascii");
}

void Dialog::render() {
    const Set s = Nat::find(chosen());
    QString t;
    const bool lower = m_lower;
    if (!m_incoming.isEmpty()) {
        t += "Files being added, as they would be written to the disc:\n";
        for (int i = 0; i < m_incoming.size() && i < 12; ++i) {
            const QString disc = Cpm::Volume::toCpmName(fromHost(m_incoming.at(i), s, !lower), extraNameChars(s) + (lower ? lowerLetters() : QString()));
            t += "  " + m_incoming.at(i) + "  ->  " + disc + "  (shown as " + toHost(disc, s) + ")\n";
        }
        if (m_incoming.size() > 12) t += QString("  ... and %1 more\n").arg(m_incoming.size() - 12);
        t += "\n";
    }
    t += "File names:\n";
    if (m_ev.names.isEmpty()) t += "  (none use those characters)\n";
    for (const QString &n : m_ev.names) t += "  " + toHost(n, s) + "\n";
    t += "\nText:\n";
    if (m_ev.sample.isEmpty()) t += "  (no text file uses them)\n";
    else t += toHost(m_ev.sample, s).left(1200);
    m_preview->setPlainText(t);
}

} // namespace Nat
