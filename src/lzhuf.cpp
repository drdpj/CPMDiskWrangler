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
// lzhuf.cpp - the decoder for TeleDisk 2.x "advanced compression": LZSS with a 4096-byte window and a 60-byte
// look-ahead, whose literals and match lengths are coded with an adaptive Huffman tree and whose match positions
// use a fixed prefix code for the top 6 bits. Everything after the 12-byte TeleDisk header is one stream.
//
// This is the LZHUF algorithm, and the code follows the structure of LZHUF.C by Haruyasu Yoshizaki (1988, English
// comments by Haruhiko Okumura, 1989), which builds on Okumura's public-domain LZSS. LZHUF.C carries no licence
// statement and has long been reused freely; this implementation was written for this program (the position-code
// tables are generated rather than copied) and is distributed under the GPL with it. See THIRD-PARTY-NOTICES.md.
#include "diskimage.h"

#include <vector>

namespace Disk {

namespace {

const int N = 4096, F = 60, THRESHOLD = 2;
const int N_CHAR = 256 - THRESHOLD + F;   // literals 0-255 and match lengths 3..60
const int T = N_CHAR * 2 - 1;             // tree size
const int R = T - 1;                      // root
const unsigned MAX_FREQ = 0x8000;

struct Decoder {
    const unsigned char *in;
    int inLen, inPos = 0;
    unsigned getbuf = 0;
    int getlen = 0;
    unsigned freq[T + 1];
    int prnt[T + N_CHAR];
    int son[T];
    unsigned char dCode[256], dLen[256];

    Decoder(const unsigned char *p, int n) : in(p), inLen(n) {
        // The position prefix code: 1 code of 3 bits, 3 of 4, 8 of 5, 12 of 6, 24 of 7, 16 of 8 bits (64 in all),
        // assigned in order, so a byte's top bits select the code. The tables give, for each possible next byte,
        // the upper 6 bits of the position and how many bits the code used.
        static const int counts[] = {1, 3, 8, 12, 24, 16};
        int b = 0, code = 0;
        for (int len = 3; len <= 8; ++len)
            for (int k = 0; k < counts[len - 3]; ++k, ++code)
                for (int r = 0; r < (1 << (8 - len)); ++r, ++b) { dCode[b] = (unsigned char)code; dLen[b] = (unsigned char)len; }
        startHuff();
    }
    int rawByte() { return inPos < inLen ? in[inPos++] : 0; }
    int getBit() {
        while (getlen <= 8) { getbuf |= unsigned(rawByte()) << (8 - getlen); getlen += 8; }
        int i = (getbuf >> 15) & 1;
        getbuf = (getbuf << 1) & 0xFFFF;
        getlen--;
        return i;
    }
    int getByte() {
        while (getlen <= 8) { getbuf |= unsigned(rawByte()) << (8 - getlen); getlen += 8; }
        int i = (getbuf >> 8) & 0xFF;
        getbuf = (getbuf << 8) & 0xFFFF;
        getlen -= 8;
        return i;
    }
    void startHuff() {
        int i, j;
        for (i = 0; i < N_CHAR; i++) { freq[i] = 1; son[i] = i + T; prnt[i + T] = i; }
        i = 0; j = N_CHAR;
        while (j <= R) {
            freq[j] = freq[i] + freq[i + 1];
            son[j] = i;
            prnt[i] = prnt[i + 1] = j;
            i += 2; j++;
        }
        freq[T] = 0xFFFF;
        prnt[R] = 0;
    }
    void reconst() {
        int i, j, k;
        unsigned f;
        j = 0;
        for (i = 0; i < T; i++)
            if (son[i] >= T) { freq[j] = (freq[i] + 1) / 2; son[j] = son[i]; j++; }
        for (i = 0, j = N_CHAR; j < T; i += 2, j++) {
            k = i + 1;
            f = freq[j] = freq[i] + freq[k];
            for (k = j - 1; f < freq[k]; k--) {}
            k++;
            for (int m = j; m > k; --m) { freq[m] = freq[m - 1]; son[m] = son[m - 1]; }
            freq[k] = f;
            son[k] = i;
        }
        for (i = 0; i < T; i++) {
            if ((k = son[i]) >= T) prnt[k] = i;
            else prnt[k] = prnt[k + 1] = i;
        }
    }
    void update(int c) {
        int i, j, k, l;
        if (freq[R] == MAX_FREQ) reconst();
        c = prnt[c + T];
        do {
            k = ++freq[c];
            if (unsigned(k) > freq[l = c + 1]) {
                while (unsigned(k) > freq[++l]) {}
                l--;
                freq[c] = freq[l];
                freq[l] = unsigned(k);
                i = son[c];
                prnt[i] = l;
                if (i < T) prnt[i + 1] = l;
                j = son[l];
                son[l] = i;
                prnt[j] = c;
                if (j < T) prnt[j + 1] = c;
                son[c] = j;
                c = l;
            }
        } while ((c = prnt[c]) != 0);
    }
    int decodeChar() {
        int c = son[R];
        while (c < T) { c += getBit(); c = son[c]; }
        c -= T;
        update(c);
        return c;
    }
    int decodePosition() {
        int i = getByte();
        int c = int(dCode[i]) << 6;
        int j = dLen[i] - 2;
        while (j--) i = (i << 1) + getBit();
        return c | (i & 0x3F);
    }
};

} // namespace

QByteArray lzhufDecode(const QByteArray &input, int maxOut) {
    Decoder d(reinterpret_cast<const unsigned char *>(input.constData()), input.size());
    std::vector<unsigned char> text(N + F - 1, ' ');
    int r = N - F;
    QByteArray out;
    out.reserve(qMin(maxOut, input.size() * 4));
    // The stream has no length: stop once the input is used up (the decoder reads zero bits past the end,
    // which can produce a little junk after the real data; the TeleDisk parser stops at its end-of-disc marker).
    while (out.size() < maxOut && d.inPos < d.inLen + 2) {
        int c = d.decodeChar();
        if (c < 256) {
            out.append(char(c));
            text[size_t(r++)] = (unsigned char)c;
            r &= N - 1;
        } else {
            int i = (r - d.decodePosition() - 1) & (N - 1);
            int j = c - 255 + THRESHOLD;
            for (int k = 0; k < j; k++) {
                unsigned char ch = text[size_t((i + k) & (N - 1))];
                out.append(char(ch));
                text[size_t(r++)] = ch;
                r &= N - 1;
            }
        }
    }
    return out;
}

} // namespace Disk
