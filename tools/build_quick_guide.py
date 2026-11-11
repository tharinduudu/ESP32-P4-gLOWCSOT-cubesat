#!/usr/bin/env python3
"""Build the two-page field quick guide from the repository photos."""

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase.pdfmetrics import stringWidth
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "docs" / "assets"
OUTPUT = ROOT / "docs" / "gLOWCOST_Detector_Quick_Guide.pdf"

PAGE_W, PAGE_H = letter
MARGIN = 46
INK = colors.HexColor("#1d2320")
MUTED = colors.HexColor("#58615c")
GREEN = colors.HexColor("#1f6f54")
PALE_GREEN = colors.HexColor("#edf5f0")
PALE_GRAY = colors.HexColor("#f2f3f1")
RULE = colors.HexColor("#cbd0cc")
WHITE = colors.white


BODY = ParagraphStyle(
    "Body",
    fontName="Helvetica",
    fontSize=10.2,
    leading=14.2,
    textColor=INK,
    alignment=TA_LEFT,
)
SMALL = ParagraphStyle(
    "Small",
    parent=BODY,
    fontSize=8.6,
    leading=11.5,
    textColor=MUTED,
)
CALLOUT = ParagraphStyle(
    "Callout",
    parent=BODY,
    fontSize=9.7,
    leading=13.5,
)
CODE = ParagraphStyle(
    "Code",
    parent=BODY,
    fontName="Courier",
    fontSize=7.7,
    leading=10,
)


def paragraph(c, text, x, y_top, width, style=BODY):
    """Draw a wrapped paragraph and return its bottom y coordinate."""
    p = Paragraph(text, style)
    _, height = p.wrap(width, PAGE_H)
    p.drawOn(c, x, y_top - height)
    return y_top - height


def section_title(c, text, x, y, width):
    c.setFont("Helvetica-Bold", 14)
    c.setFillColor(INK)
    c.drawString(x, y, text)
    c.setStrokeColor(RULE)
    c.setLineWidth(0.8)
    c.line(x, y - 7, x + width, y - 7)


def page_header(c, title, subtitle, page_no):
    c.setFillColor(INK)
    c.setFont("Helvetica-Bold", 25)
    c.drawString(MARGIN, PAGE_H - 54, title)
    c.setFont("Helvetica", 10.5)
    c.setFillColor(MUTED)
    c.drawString(MARGIN, PAGE_H - 72, subtitle)
    c.setStrokeColor(INK)
    c.setLineWidth(0.8)
    c.line(MARGIN, PAGE_H - 86, PAGE_W - MARGIN, PAGE_H - 86)
    c.setFont("Helvetica", 8)
    c.setFillColor(MUTED)
    c.drawRightString(PAGE_W - MARGIN, 25, f"gLOWCOST detector quick guide  |  {page_no}/2")


def draw_image_fit(c, path, x, y, width, height):
    """Fit an image into a frame without changing its aspect ratio."""
    image = ImageReader(str(path))
    image_w, image_h = image.getSize()
    scale = min(width / image_w, height / image_h)
    draw_w = image_w * scale
    draw_h = image_h * scale
    draw_x = x + (width - draw_w) / 2
    draw_y = y + (height - draw_h) / 2
    c.setFillColor(PALE_GRAY)
    c.rect(x, y, width, height, fill=1, stroke=0)
    c.drawImage(
        image,
        draw_x,
        draw_y,
        draw_w,
        draw_h,
        preserveAspectRatio=True,
        mask="auto",
    )


def numbered_steps(c, steps, x, y_top, width, number_width=20, gap=8):
    y = y_top
    for number, text in enumerate(steps, 1):
        c.setFillColor(GREEN)
        c.circle(x + 7, y - 6, 7, fill=1, stroke=0)
        c.setFillColor(WHITE)
        c.setFont("Helvetica-Bold", 8)
        label = str(number)
        c.drawString(x + 7 - stringWidth(label, "Helvetica-Bold", 8) / 2, y - 9, label)
        y = paragraph(c, text, x + number_width, y, width - number_width, BODY) - gap
    return y


def checklist(c, items, x, y_top, width, leading=23):
    y = y_top
    for item in items:
        c.setStrokeColor(MUTED)
        c.setLineWidth(0.7)
        c.rect(x, y - 9, 8, 8, fill=0, stroke=1)
        paragraph(c, item, x + 16, y, width - 16, BODY)
        y -= leading
    return y


def callout(c, title, text, x, y, width, height, fill=PALE_GREEN):
    c.setFillColor(fill)
    c.setStrokeColor(RULE)
    c.roundRect(x, y, width, height, 5, fill=1, stroke=1)
    c.setFillColor(GREEN)
    c.setFont("Helvetica-Bold", 11.5)
    c.drawString(x + 14, y + height - 22, title)
    paragraph(c, text, x + 14, y + height - 34, width - 28, CALLOUT)


def code_box(c, text, x, y, width, height):
    c.setFillColor(PALE_GRAY)
    c.setStrokeColor(RULE)
    c.roundRect(x, y, width, height, 4, fill=1, stroke=1)
    paragraph(c, text, x + 11, y + height - 11, width - 22, CODE)


def draw_page_one(c):
    page_header(c, "gLOWCOST detector quick guide", "ESP32-P4 field start-up and Wi-Fi setup", 1)

    section_title(c, "Start a run", MARGIN, 678, 245)
    numbered_steps(
        c,
        [
            "Insert a FAT32-formatted microSD card before power-up.",
            "Connect the detector to the USB power bank. It starts automatically.",
            "Leave the detector still while the FPGA, DAC, and high voltage are configured.",
            "Wait through the 10 second HV settling period. Counting and SD logging then begin.",
        ],
        MARGIN,
        651,
        245,
    )

    draw_image_fit(c, ASSETS / "detector-open-case.jpg", 325, 505, 241, 181)
    c.setFont("Helvetica", 8.3)
    c.setFillColor(MUTED)
    c.drawString(325, 493, "Detector opened for setup")

    section_title(c, "Open the detector page", MARGIN, 459, PAGE_W - 2 * MARGIN)
    paragraph(
        c,
        "Turn mobile data off so the phone stays connected to the detector network.",
        MARGIN,
        436,
        PAGE_W - 2 * MARGIN,
        BODY,
    )

    cell_y = 380
    cell_w = (PAGE_W - 2 * MARGIN - 16) / 3
    labels = [("Wi-Fi", "MuonReadout"), ("Password", "glowcost"), ("Web page", "192.168.4.1")]
    for idx, (label, value) in enumerate(labels):
        x = MARGIN + idx * (cell_w + 8)
        c.setFillColor(PALE_GRAY)
        c.rect(x, cell_y, cell_w, 42, fill=1, stroke=0)
        c.setFillColor(MUTED)
        c.setFont("Helvetica", 8.4)
        c.drawString(x + 10, cell_y + 26, label)
        c.setFillColor(INK)
        c.setFont("Helvetica-Bold", 10)
        c.drawString(x + 10, cell_y + 10, value)

    section_title(c, "Before the measurement", MARGIN, 345, 310)
    checklist(
        c,
        [
            "SD Card shows Ready.",
            "Browser time has synced to the detector.",
            "One-minute coincidence counts look reasonable.",
            "Flight number, location, or run label is set if needed.",
            "Wi-Fi is off before the quiet measurement period.",
        ],
        MARGIN,
        319,
        306,
    )

    callout(
        c,
        "Five-minute Wi-Fi window",
        "A countdown is shown on the web page. Wi-Fi turns off five minutes after startup, even if a phone remains connected. Select <b>Keep Wi-Fi On</b> only when a longer setup session is needed. The choice lasts until the next power cycle.",
        382,
        207,
        184,
        139,
    )

    photo_y = 62
    photo_w = 158
    photo_h = 112
    photos = [
        ("detector-closed-case.jpg", "Closed case"),
        ("detector-usb-power-bank.jpg", "Powered detector"),
        ("power-bank.jpg", "5000 mAh power bank"),
    ]
    for idx, (name, caption) in enumerate(photos):
        x = MARGIN + idx * (photo_w + 18)
        draw_image_fit(c, ASSETS / name, x, photo_y, photo_w, photo_h)
        c.setFont("Helvetica", 8.1)
        c.setFillColor(MUTED)
        c.drawCentredString(x + photo_w / 2, photo_y - 12, caption)


def draw_page_two(c):
    page_header(c, "Wired USB-UART monitor", "Use the Mac dashboard while detector Wi-Fi remains off", 2)

    draw_image_fit(c, ASSETS / "detector-usb-power-bank.jpg", MARGIN, 498, 258, 193)
    c.setFont("Helvetica", 8.3)
    c.setFillColor(MUTED)
    c.drawString(MARGIN, 486, "Keep the detector powered from the power bank during monitoring.")

    section_title(c, "Connect the Mac", 329, 678, 237)
    numbered_steps(
        c,
        [
            "Leave the normal USB-C power input connected to the power bank.",
            "Connect the separate USB-UART Type-C port on the ESP32-P4 board to the Mac.",
            "Start the Mac monitor and keep its Terminal window open.",
            "Open <b>http://127.0.0.1:8080</b> if the browser does not open automatically.",
        ],
        329,
        651,
        237,
        gap=6,
    )

    section_title(c, "Get and start the Mac monitor", MARGIN, 449, PAGE_W - 2 * MARGIN)
    c.setFillColor(INK)
    c.setFont("Helvetica-Bold", 10.5)
    c.drawString(MARGIN, 423, "Download the monitor")
    paragraph(
        c,
        "Go to <b>github.com/tharinduudu/ESP32-P4-gLOWCSOT-cubesat</b>. Open <b>dist/Muon-UART-Monitor-macOS.zip</b>, then click <b>Download raw file</b>.",
        MARGIN,
        408,
        310,
        SMALL,
    )
    paragraph(
        c,
        "The ZIP contains <b>uart_web_monitor.py</b>, the Mac launcher, and a Read Me.",
        MARGIN,
        366,
        310,
        SMALL,
    )

    c.setFillColor(INK)
    c.setFont("Helvetica-Bold", 10.5)
    c.drawString(382, 423, "Start it")
    paragraph(
        c,
        "Unzip the download and double-click <b>Start Muon UART Monitor.command</b>. If macOS blocks it, right-click and choose <b>Open</b>. Keep the Terminal window open.",
        382,
        408,
        184,
        SMALL,
    )

    c.setFillColor(INK)
    c.setFont("Helvetica-Bold", 10.5)
    c.drawString(MARGIN, 315, "The wired page provides")
    checklist(
        c,
        [
            "Latest 10 count lines, with the newest line highlighted.",
            "Automatic time sync from the Mac browser.",
            "SD, FPGA, HV, counting, and environment status.",
            "Run-label changes and SD file downloads.",
            "Protected Wi-Fi shutdown and FPGA reflash controls.",
        ],
        MARGIN,
        293,
        318,
        leading=22,
    )

    callout(
        c,
        "The detector keeps running",
        "Keep the power bank connected. Closing the Mac program or unplugging the UART cable does not reset the detector or stop its counters and SD logging.",
        382,
        185,
        184,
        136,
    )

    callout(
        c,
        "Time and filenames",
        "Opening the page sends the Mac's local time to the detector. The first valid sync renames the active muon and environment files with that timestamp.",
        382,
        40,
        184,
        134,
        fill=PALE_GRAY,
    )

    section_title(c, "If the page does not open", MARGIN, 150, 310)
    paragraph(
        c,
        "Confirm Python 3 is installed and a serial device appears with <b>ls /dev/cu.usbmodem*</b>. Close <b>screen</b>, miniterm, or any other program using the UART port, then restart the monitor.",
        MARGIN,
        124,
        306,
        BODY,
    )

    c.setStrokeColor(GREEN)
    c.setLineWidth(1.2)
    c.line(MARGIN, 50, 360, 50)
    c.setFillColor(GREEN)
    c.setFont("Helvetica-Bold", 9.5)
    c.drawString(MARGIN, 35, "Wi-Fi is not required for the wired dashboard.")


def build():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    c = canvas.Canvas(str(OUTPUT), pagesize=letter)
    c.setTitle("gLOWCOST Detector Quick Guide")
    c.setAuthor("Tharindu Hettiarachchi")
    c.setSubject("ESP32-P4 detector start-up, Wi-Fi, and USB-UART monitoring")
    draw_page_one(c)
    c.showPage()
    draw_page_two(c)
    c.save()
    print(OUTPUT)


if __name__ == "__main__":
    build()
