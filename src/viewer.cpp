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
#include "viewer.h"
#include "natchars.h"
#include "viewtext.h"

#include <diskwrangler/text/viewers.h>

// CP/M text: up to the first ^Z (the rest of the last record is padding), optionally with bit 7 ignored (WordStar), shown
// in the national character set the dialog was given (the "nat" entry of the context).
class CpmTextViewer : public dw::FileViewer {
public:
    QString id() const override { return "cpmtext"; }
    QString name() const override { return "Text"; }
    int order() const override { return 0; }
    int accepts(const QByteArray &d, const QVariantMap &) const override { return ViewText::looksLikeText(d) ? 2 : 0; }
    QVector<dw::ViewOption> options(const QByteArray &d) const override {
        return {{"eof", "Stop at ^Z", "CP/M text files end at the first ^Z (1A hex); the rest of the last record is padding", true},
                {"strip", "Ignore bit 7 (WordStar)", QString(), ViewText::looksLikeWordStar(d)}};
    }
    dw::ViewResult render(const QByteArray &d, const QVariantMap &o) const override {
        dw::ViewResult r;
        int eof = -1;
        r.text = Nat::toHost(ViewText::cpmText(d, o["eof"].toBool(), o["strip"].toBool(), &eof), Nat::find(o["nat"].toString()));
        if (eof >= 0) r.note = QString("^Z (end of text) at byte %1").arg(eof);
        return r;
    }
};

static const dw::ViewerRegistry &cpmViewers() {
    static dw::ViewerRegistry reg = [] {
        dw::ViewerRegistry r;
        dw::addDefaultViewers(r);
        r.remove("plain");                      // CP/M's own Text replaces the core's
        r.add(std::make_shared<CpmTextViewer>());
        return r;
    }();
    return reg;
}

ViewerDialog::ViewerDialog(const QString &title, const QByteArray &data, QWidget *parent, const QString &natId)
    : dw::ViewerDialog(cpmViewers(), title, data, parent, QVariantMap{{"nat", natId}}) {}
