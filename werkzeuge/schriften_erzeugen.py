"""Erzeugt die geglaetteten Schriften fuer das Display (src/schriften.h).

Schrift: Lato (Lukasz Dziedzic, SIL Open Font License 1.1 - darf eingebettet werden).
Die TTF-Dateien kommen aus C:\\Windows\\Fonts (oder per Kommandozeile: Ordner mit Lato-*.ttf).
Nur noetig, wenn sich Schriften aendern sollen - src/schriften.h liegt fertig im Repo.

Format: VLW wie es TFT_eSPI mit loadFont() liest (siehe TFT_eSPI/Extensions/Smooth_font.cpp):
  Kopf:     6 x int32 (big endian): Zeichenzahl, Version, Groesse, 0, Ascent, Descent
  je Zeichen 7 x int32: Unicode, Hoehe, Breite, Vorschub, Abstand Grundlinie->oben, Abstand links, 0
  danach die Bitmaps (1 Byte Deckkraft je Pixel) in derselben Reihenfolge.

Aufruf (im Projektordner):  python werkzeuge/schriften_erzeugen.py [Ordner mit Lato-*.ttf]
"""
import struct
import sys
from pathlib import Path

from PIL import Image, ImageFont

FAKTOR = 4  # in 4-facher Groesse zeichnen, dann verkleinern (besonders weiche Kanten)
ORDNER = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"C:\Windows\Fonts")

TEXT = [chr(c) for c in range(0x20, 0x7F)] + [chr(c) for c in range(0xA0, 0x100)] + list("–—…‘’‚“”„•€")
ZIFFERN = list(" 0123456789-+.,:%°")

# Name, Pixelgroesse, Schnitt, Zeichen
SCHRIFTEN = [
    ("SCHRIFT_K", 15, "Regular", TEXT),     # klein: Zusatzinfos (ersetzt Font2)
    ("SCHRIFT_M", 19, "Regular", TEXT),     # mittel: Pollen, Stundenwerte, Anleitungen
    ("SCHRIFT_MF", 19, "Bold", TEXT),       # mittel fett: Titel, Uhrzeit
    ("SCHRIFT_G", 25, "Regular", TEXT),     # gross: Wetterbeschreibung
    ("SCHRIFT_GF", 25, "Bold", TEXT),       # gross fett: Ueberschriften der Sonderbilder
    ("SCHRIFT_XG", 40, "Bold", ZIFFERN),    # sehr gross: Tageshoechstwert, Luftindex
    ("SCHRIFT_RIESIG", 64, "Black", ZIFFERN),  # Temperatur auf Seite 1
]


def zeichen(font, ch, kontur=0):
    """Bitmap und Masse eines Zeichens in Zielgroesse."""
    maske, (dx, dy) = font.getmask2(ch, mode="L", anchor="ls")
    w, h = maske.size
    vorschub = font.getlength(ch)
    if w == 0 or h == 0:
        return b"", 0, 0, round(vorschub / FAKTOR), 0, 0
    bild = Image.frombytes("L", (w, h), bytes(maske))
    # auf ein Raster legen, das durch FAKTOR teilbar ist, damit die Verkleinerung sauber bleibt
    x0, y0 = dx - (dx % FAKTOR), dy - (dy % FAKTOR)
    W = -(-(dx + w - x0) // FAKTOR) * FAKTOR
    H = -(-(dy + h - y0) // FAKTOR) * FAKTOR
    gross = Image.new("L", (W, H), 0)
    gross.paste(bild, (dx - x0, dy - y0))
    klein = gross.resize((W // FAKTOR, H // FAKTOR), Image.BOX)
    # leere Raender abschneiden
    box = klein.getbbox()
    if not box:
        return b"", 0, 0, round(vorschub / FAKTOR), 0, 0
    klein = klein.crop(box)
    links = x0 // FAKTOR + box[0]
    oben = -(y0 // FAKTOR + box[1])  # Abstand von der Grundlinie nach oben
    return klein.tobytes(), klein.size[1], klein.size[0], round(vorschub / FAKTOR), oben, links


def vlw(groesse, schnitt, zeichenliste):
    font = ImageFont.truetype(str(ORDNER / f"Lato-{schnitt}.ttf"), groesse * FAKTOR)
    # Ascent/Descent wie TFT_eSPI sie fuer Grundlinie und Zeilenhoehe nutzt: Oberkante "d", Unterkante "p"
    ascent = round(-font.getbbox("d", anchor="ls")[1] / FAKTOR)
    descent = round(font.getbbox("p", anchor="ls")[3] / FAKTOR)
    kopf = struct.pack(">6i", len(zeichenliste), 11, groesse, 0, ascent, descent)
    masse, bitmaps = b"", b""
    for ch in zeichenliste:
        daten, h, w, vor, oben, links = zeichen(font, ch)
        if w > 255 or h > 255:
            sys.exit(f"Zeichen {ch!r} zu gross")
        masse += struct.pack(">7i", ord(ch), h, w, vor, oben, links, 0)
        bitmaps += daten
    return kopf + masse + bitmaps


def main():
    ziel = Path(__file__).resolve().parent.parent / "src" / "schriften.h"
    teile = [
        "#pragma once",
        "// Geglaettete Schriften fuer TFT_eSPI (loadFont), erzeugt mit werkzeuge/schriften_erzeugen.py",
        "// Schrift: Lato von Lukasz Dziedzic, SIL Open Font License 1.1 (https://openfontlicense.org)",
        "// - nicht von Hand aendern.",
        "#include <Arduino.h>",
        "",
    ]
    for name, groesse, schnitt, zeichenliste in SCHRIFTEN:
        daten = vlw(groesse, schnitt, zeichenliste)
        zeilen = [", ".join(f"0x{b:02X}" for b in daten[i:i + 24]) for i in range(0, len(daten), 24)]
        teile.append(f"// Lato {schnitt} {groesse} px, {len(zeichenliste)} Zeichen, {len(daten)} Byte")
        teile.append(f"static const uint8_t {name}[] PROGMEM = {{\n  " + ",\n  ".join(zeilen) + "\n};\n")
        print(f"{name:16} {groesse:3} px  {len(zeichenliste):3} Zeichen  {len(daten):7} Byte")
    ziel.write_text("\n".join(teile), encoding="utf-8")
    print("->", ziel)


if __name__ == "__main__":
    main()
