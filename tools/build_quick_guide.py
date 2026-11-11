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
MONITOR_URL = "https://github.com/tharinduudu/ESP32-P4-gLOWCSOT-cubesat/releases/download/uart-monitor-v1.1.0/uart_web_monitor.py"

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
    section_title(c, "1. Download and run the monitor", MARGIN, 678, PAGE_W - 2 * MARGIN)
    paragraph(
        c,
        f"Before the flight, connect the Mac to the internet and click <link href='{MONITOR_URL}' color='#1f6f54'><b>Download uart_web_monitor.py</b></link>.",
        MARGIN, 653, 315, BODY,
    )
    paragraph(
        c,
        "Run the downloaded file on the Mac. The local monitor page opens automatically; leave the Terminal window running.",
        MARGIN, 607, 315, BODY,
    )
    callout(
        c, "Dependencies",
        "macOS and Python 3.10 or newer. The script uses Python's standard library; no additional packages are required.",
        383, 572, 183, 104, fill=PALE_GRAY,
    )

    section_title(c, "2. Connect the detector", MARGIN, 540, PAGE_W - 2 * MARGIN)
    draw_image_fit(c, ASSETS / "detector-usb-power-bank.jpg", 380, 382, 186, 139)
    paragraph(
        c,
        "Keep the power bank connected to the detector's normal USB-C power input. The <b>left USB-C port is the USB-UART port</b>. Connect it to the Mac with a data-capable USB cable.",
        MARGIN, 515, 310, BODY,
    )
    paragraph(
        c,
        "The monitor works with detector Wi-Fi off. Leave the power bank connected for the entire run.",
        MARGIN, 450, 310, BODY,
    )

    section_title(c, "3. Check the run", MARGIN, 356, PAGE_W - 2 * MARGIN)
    numbered_steps(
        c,
        [
            "If the browser did not open, visit <b>http://127.0.0.1:8080</b> on the Mac.",
            "Check <b>UART connected</b>, SD status, the detector time, and the latest counts.",
            "Set a flight or location label if needed. Download SD files from the page.",
        ],
        MARGIN, 331, PAGE_W - 2 * MARGIN, gap=7,
    )
    paragraph(
        c,
        "The monitor uses a local page on the Mac. Detector Wi-Fi is not required.",
        MARGIN, 205, PAGE_W - 2 * MARGIN, SMALL,
    )
    section_title(c, "During the run", MARGIN, 157, PAGE_W - 2 * MARGIN)
    paragraph(
        c,
        "The browser sends the Mac's time to the detector; confirm the displayed time. The first valid sync renames the active files. Closing Terminal or unplugging the Mac leaves counting and SD logging running on the power bank.",
        MARGIN, 132, PAGE_W - 2 * MARGIN, SMALL,
    )
    paragraph(
        c,
        "<b>UART unavailable?</b> Check the USB-UART port and data cable. Close screen or other serial programs and restart the monitor. In Terminal, <b>ls /dev/cu.usbmodem*</b> lists detected USB serial ports.",
        MARGIN, 79, PAGE_W - 2 * MARGIN, SMALL,
    )


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
