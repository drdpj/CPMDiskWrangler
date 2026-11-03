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
// detect.h - working out a CP/M disc's logical format from its contents.
//
// The physical shape (sector size, sectors per track, sides, sector IDs) comes from the container, or for a raw
// image from the file size. The logical layout (reserved tracks, skew, side order, block size, directory size)
// is then searched for: every plausible combination is scored on what it makes of the directory (valid entries,
// block pointers in range and not shared, block counts that match record counts, files starting just after the
// directory) and, for the best ones, on the files' contents (text files that stay text up to their ^Z, files
// that don't start in never-written space). Known formats from the library and a few physical clues (sector
// ID base, the Amstrad PCW boot-sector spec) add to the score. See AGENTS.md for the reasoning.
#pragma once

#include <QStringList>
#include <QVector>

#include "cpmformat.h"
#include "diskimage.h"

namespace Detect {

struct Candidate {
    Cpm::Format fmt;                 // derived; name = library name, or "detected"
    Disk::RawLayout raw;             // raw images: the physical layout this reading uses
    double score = 0;
    bool fromLibrary = false;        // the layout equals a library format
    bool remembered = false;         // the user chose this format for this image before
    int files = 0, fileEntries = 0, badEntries = 0, badPointers = 0, crossLinks = 0, exactCounts = 0;
    int widePointers = 0;            // entries whose block pointers read as two bytes wide (see scoreDir)
    int textGood = 0, textBad = 0, startsBlank = 0;
    QStringList evidence;            // for the format dialog
    double base = 0;                 // the part of score that evaluate() gave (the rest: known format, IDs, ...)
    int baseLines = 0;               // the evidence lines evaluate() wrote (the first ones)
    QString key() const;             // identifies the layout (for removing duplicates)
};

struct Result {
    QVector<Candidate> ranked;       // best first, one per distinct layout
    QString physical;                // the physical shape, in words
    QString confidence;              // "high", "medium", "low", "none"
    bool safeToWrite = false;        // the best reading is unambiguous enough to allow changes
    QString why;                     // why it is or isn't safe
};

// Scores one format against an image (raw images must already have their layout set). Used by detect() and by
// the format dialog when the user picks or edits a format by hand.
Candidate evaluate(const Disk::Image &img, const Cpm::Format &f, bool deep);
// The points detection adds to a reading that has the layout of a library format (its definition in lib): +3 for being a
// known format, track 0's shape, the Acorn DFS catalogue, whether the sector IDs start where the definition says. The
// format dialog uses it too, so a format typed by hand that equals a library one scores the same as the detected one.
void scoreKnownFormat(Candidate *c, const Cpm::Format &lib, const Disk::Image &img);

// The same reading scored again on the disc as it is now (files added or deleted since detection, a new disc
// that has been filled): what evaluate() found is replaced, the bonuses detection added are kept. A raw image
// must already have the candidate's layout set.
Candidate reevaluate(const Disk::Image &img, const Candidate &c);

// rememberedDiskdef: a format the user confirmed for this file earlier (diskdefs text), or empty.
// For a raw image, the image's layout is set to the winner's before returning.
Result detect(Disk::Image &img, const QString &rememberedDiskdef = QString(), int keep = 25);

} // namespace Detect
