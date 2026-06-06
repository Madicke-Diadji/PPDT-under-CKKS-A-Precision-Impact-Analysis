"""
Rapport PDF complet du POC My_POC_All
Auteur : Madicke Mbodj — Mai 2026
"""

import csv
import os
from datetime import datetime
from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import cm, mm
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_JUSTIFY, TA_RIGHT
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
    PageBreak, HRFlowable
)
from reportlab.graphics.shapes import Drawing, Rect, Line, String
from reportlab.graphics.charts.barcharts import VerticalBarChart
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

# ─── Enregistrement des polices TTF (support Unicode / accents) ─────────────────
FONTS = "C:/Windows/Fonts/"

def reg(name, path):
    try:
        pdfmetrics.registerFont(TTFont(name, FONTS + path))
        return True
    except Exception:
        return False

reg("Arial",         "arial.ttf")
reg("Arial-Bold",    "arialbd.ttf")
reg("Arial-Italic",  "ariali.ttf")
reg("Arial-BoldItalic", "arialbi.ttf")
reg("Consolas",      "consola.ttf")
reg("Consolas-Bold", "consolab.ttf")

from reportlab.pdfbase.pdfmetrics import registerFontFamily
try:
    registerFontFamily("Arial",
        normal="Arial", bold="Arial-Bold",
        italic="Arial-Italic", boldItalic="Arial-BoldItalic")
except Exception:
    pass

# Noms de polices utilisés
F_BODY   = "Arial"
F_BOLD   = "Arial-Bold"
F_ITALIC = "Arial-Italic"
F_CODE   = "Consolas"
F_CODE_B = "Consolas-Bold"

# ─── Palette ────────────────────────────────────────────────────────────────────
DARK_BLUE    = colors.HexColor("#1A237E")
MID_BLUE     = colors.HexColor("#283593")
LIGHT_BLUE   = colors.HexColor("#3F51B5")
ACCENT_BLUE  = colors.HexColor("#42A5F5")
PALE_BLUE    = colors.HexColor("#E3F2FD")
TEAL         = colors.HexColor("#00695C")
LIGHT_TEAL   = colors.HexColor("#E0F2F1")
ORANGE       = colors.HexColor("#E65100")
LIGHT_ORANGE = colors.HexColor("#FFF3E0")
GRAY_DARK    = colors.HexColor("#212121")
GRAY_MID     = colors.HexColor("#616161")
GRAY_LIGHT   = colors.HexColor("#F5F5F5")
GRAY_BORDER  = colors.HexColor("#BDBDBD")
WHITE        = colors.white
RED_WARN     = colors.HexColor("#B71C1C")
GREEN_OK     = colors.HexColor("#1B5E20")

OUTPUT_PATH = os.path.join(os.path.dirname(__file__), "POC_My_Poc_all_Report.pdf")
RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
RUN_RESULTS_SOFT = os.path.join(RESULTS_DIR, "run_results_soft_adaptatif.csv")
RUN_RESULTS_AKAVIA = os.path.join(RESULTS_DIR, "run_results.csv")
RUN_RESULTS_SORTINGHAT = os.path.join(RESULTS_DIR, "run_results_sortinghat_transciphering.csv")
MONTHS_FR = {
    1: "janvier", 2: "fevrier", 3: "mars", 4: "avril",
    5: "mai", 6: "juin", 7: "juillet", 8: "aout",
    9: "septembre", 10: "octobre", 11: "novembre", 12: "decembre"
}

# ─── Styles ─────────────────────────────────────────────────────────────────────
def build_styles():
    s = {}
    s["h1"] = ParagraphStyle(
        "h1", fontName=F_BOLD, fontSize=14, leading=20,
        textColor=WHITE, backColor=DARK_BLUE,
        spaceBefore=0, spaceAfter=10,
        borderPadding=(6, 10, 6, 10)
    )
    s["h2"] = ParagraphStyle(
        "h2", fontName=F_BOLD, fontSize=12, leading=17,
        textColor=DARK_BLUE, spaceBefore=14, spaceAfter=6,
        borderPadding=(0, 0, 2, 0)
    )
    s["h3"] = ParagraphStyle(
        "h3", fontName=F_BOLD, fontSize=10.5, leading=15,
        textColor=MID_BLUE, spaceBefore=10, spaceAfter=4
    )
    s["body"] = ParagraphStyle(
        "body", fontName=F_BODY, fontSize=9.5, leading=14,
        textColor=GRAY_DARK, alignment=TA_JUSTIFY, spaceAfter=5
    )
    s["bullet"] = ParagraphStyle(
        "bullet", fontName=F_BODY, fontSize=9.5, leading=14,
        textColor=GRAY_DARK, leftIndent=14, spaceAfter=3
    )
    s["code"] = ParagraphStyle(
        "code", fontName=F_CODE, fontSize=8, leading=12,
        textColor=GRAY_DARK, backColor=colors.HexColor("#F3F4F6"),
        borderColor=GRAY_BORDER, borderWidth=0.5,
        borderPadding=7, spaceAfter=6
    )
    s["caption"] = ParagraphStyle(
        "caption", fontName=F_ITALIC, fontSize=8.5, leading=12,
        textColor=GRAY_MID, alignment=TA_CENTER, spaceAfter=8
    )
    s["note"] = ParagraphStyle(
        "note", fontName=F_ITALIC, fontSize=9, leading=13,
        textColor=TEAL, backColor=LIGHT_TEAL,
        borderPadding=6, spaceAfter=6
    )
    s["warn"] = ParagraphStyle(
        "warn", fontName=F_ITALIC, fontSize=9, leading=13,
        textColor=RED_WARN, backColor=colors.HexColor("#FFEBEE"),
        borderPadding=6, spaceAfter=6
    )
    s["toc_entry"] = ParagraphStyle(
        "toc_entry", fontName=F_BODY, fontSize=10, leading=15,
        textColor=DARK_BLUE, spaceAfter=3
    )
    s["toc_page"] = ParagraphStyle(
        "toc_page", fontName=F_BOLD, fontSize=10, leading=15,
        textColor=GRAY_MID, alignment=TA_RIGHT
    )
    s["cell"] = ParagraphStyle(
        "cell", fontName=F_BODY, fontSize=8.5, leading=12,
        textColor=GRAY_DARK
    )
    s["cell_bold"] = ParagraphStyle(
        "cell_bold", fontName=F_BOLD, fontSize=8.5, leading=12,
        textColor=GRAY_DARK
    )
    s["cell_header"] = ParagraphStyle(
        "cell_header", fontName=F_BOLD, fontSize=8.5, leading=12,
        textColor=WHITE
    )
    return s

# ─── Helpers ────────────────────────────────────────────────────────────────────
def h1(text, s):
    return Paragraph(f"  {text}", s["h1"])

def h2(text, s):
    return Paragraph(text, s["h2"])

def h3(text, s):
    return Paragraph(text, s["h3"])

def body(text, s):
    return Paragraph(text, s["body"])

def bitem(text, s):
    return Paragraph(f"•  {text}", s["bullet"])

def code(text, s):
    safe = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return Paragraph(safe.replace("\n", "<br/>"), s["code"])

def note(text, s):
    return Paragraph(f"<b>Note :</b>  {text}", s["note"])

def warn(text, s):
    return Paragraph(f"<b>Attention :</b>  {text}", s["warn"])

def sp(n=1):
    return Spacer(1, n * 4 * mm)

def hr():
    return HRFlowable(width="100%", thickness=0.5, color=GRAY_BORDER, spaceAfter=4)


def load_csv_rows(path):
    if not os.path.exists(path):
        return []
    with open(path, newline="", encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))


def latest_rows_by_dataset(rows, dataset_key="dataset", timestamp_key="timestamp", preferred_key=None):
    latest = {}
    for row in rows:
        dataset = row.get(dataset_key, "")
        timestamp = row.get(timestamp_key, "")
        score = 1 if (preferred_key and row.get(preferred_key, "").strip()) else 0
        current = latest.get(dataset)
        if current is None:
            latest[dataset] = row
            continue
        current_score = 1 if (preferred_key and current.get(preferred_key, "").strip()) else 0
        if score > current_score or (score == current_score and timestamp > current.get(timestamp_key, "")):
            latest[dataset] = row
    return latest


def parse_float(value):
    if value is None:
        return None
    text = str(value).strip()
    if not text:
        return None
    text = text.replace("%", "").replace(" ", "").replace(",", ".")
    try:
        return float(text)
    except ValueError:
        return None


def fmt_pct(value, digits=2):
    num = parse_float(value)
    if num is None:
        return "--"
    return f"{num:.{digits}f} %"


def fmt_ms(value, digits=2):
    num = parse_float(value)
    if num is None:
        return "--"
    return f"{num:.{digits}f} ms"


def fmt_delta_pct(value, digits=2):
    if value is None:
        return "--"
    return f"{value:+.{digits}f} %"


def fmt_status_from_row(row):
    if row.get("he_status") == "failed" or row.get("he_batch_status") == "failed":
        return "ECHEC"
    if row.get("he_status") == "ok" or row.get("he_batch_status") == "ok":
        return "ok"
    return row.get("he_status") or row.get("he_batch_status") or "--"


def short_dataset_name(name):
    if not name:
        return ""
    if "/" in name:
        return name.split("/")[-1]
    return name


def display_dataset_name(name):
    short = short_dataset_name(name)
    return short[:1].upper() + short[1:] if short else "--"


def parse_timestamp(value):
    if not value:
        return None
    text = str(value).strip()
    if not text:
        return None
    try:
        return datetime.fromisoformat(text)
    except ValueError:
        return None


def format_date_fr(dt):
    if dt is None:
        return "--"
    return f"{dt.day} {MONTHS_FR[dt.month]} {dt.year}"


def format_month_year_fr(dt):
    if dt is None:
        return "--"
    return f"{MONTHS_FR[dt.month].capitalize()} {dt.year}"


def latest_results_timestamp():
    timestamps = []
    for path in (RUN_RESULTS_SOFT, RUN_RESULTS_AKAVIA, RUN_RESULTS_SORTINGHAT):
        for row in load_csv_rows(path):
            dt = parse_timestamp(row.get("timestamp"))
            if dt is not None:
                timestamps.append(dt)
    if timestamps:
        return max(timestamps)
    return datetime.now()


def summarize_poc_latest(latest_rows):
    rows = list(latest_rows.values())
    if not rows:
        return {
            "count": 0,
            "avg_hard_gap": None,
            "avg_he_gap": None,
            "worst_soft_gap": None,
            "worst_he_gap": None,
            "he_matches_clear": [],
            "he_beats_global": [],
        }

    hard_gaps = []
    he_gaps = []
    worst_soft_gap = None
    worst_he_gap = None
    he_matches_clear = []
    he_beats_global = []

    for row in rows:
        dataset = row.get("dataset", "")
        hard_acc = parse_float(row.get("clear_hard_accuracy_pct"))
        adapt_acc = parse_float(row.get("clear_soft_adaptive_accuracy_pct"))
        global_he_acc = parse_float(row.get("he_soft_global_accuracy_pct"))
        adapt_he_acc = parse_float(row.get("he_soft_adaptive_accuracy_pct"))

        if hard_acc is not None and adapt_acc is not None:
            gap = hard_acc - adapt_acc
            hard_gaps.append(gap)
            if worst_soft_gap is None or gap > worst_soft_gap[1]:
                worst_soft_gap = (dataset, gap)

        if adapt_acc is not None and adapt_he_acc is not None:
            gap = adapt_acc - adapt_he_acc
            he_gaps.append(gap)
            if abs(gap) < 1e-9:
                he_matches_clear.append(dataset)
            if worst_he_gap is None or gap > worst_he_gap[1]:
                worst_he_gap = (dataset, gap)

        if adapt_he_acc is not None and global_he_acc is not None and adapt_he_acc > global_he_acc:
            he_beats_global.append(dataset)

    return {
        "count": len(rows),
        "avg_hard_gap": (sum(hard_gaps) / len(hard_gaps)) if hard_gaps else None,
        "avg_he_gap": (sum(he_gaps) / len(he_gaps)) if he_gaps else None,
        "worst_soft_gap": worst_soft_gap,
        "worst_he_gap": worst_he_gap,
        "he_matches_clear": he_matches_clear,
        "he_beats_global": he_beats_global,
    }

# ─── Tableaux ────────────────────────────────────────────────────────────────────
def _wrap(val, s, bold=False, header=False):
    if header:
        key = "cell_header"
    else:
        key = "cell_bold" if bold else "cell"
    if isinstance(val, str):
        return Paragraph(val, s[key])
    return val

def make_table(data, col_widths, s, header_bg=DARK_BLUE, alt=PALE_BLUE):
    wrapped = []
    for ri, row in enumerate(data):
        wrapped.append([_wrap(c, s, bold=(ri == 0), header=(ri == 0)) for c in row])
    cmds = [
        ("BACKGROUND",    (0, 0), (-1, 0), header_bg),
        ("TEXTCOLOR",     (0, 0), (-1, 0), WHITE),
        ("FONTNAME",      (0, 0), (-1, 0), F_BOLD),
        ("FONTSIZE",      (0, 0), (-1, 0), 9),
        ("FONTNAME",      (0, 1), (-1, -1), F_BODY),
        ("FONTSIZE",      (0, 1), (-1, -1), 8.5),
        ("ALIGN",         (0, 0), (-1, -1), "LEFT"),
        ("VALIGN",        (0, 0), (-1, -1), "TOP"),
        ("GRID",          (0, 0), (-1, -1), 0.4, GRAY_BORDER),
        ("TOPPADDING",    (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("LEFTPADDING",   (0, 0), (-1, -1), 6),
        ("RIGHTPADDING",  (0, 0), (-1, -1), 6),
    ]
    for i in range(1, len(data)):
        if i % 2 == 0:
            cmds.append(("BACKGROUND", (0, i), (-1, i), alt))
    t = Table(wrapped, colWidths=col_widths, repeatRows=1)
    t.setStyle(TableStyle(cmds))
    return t

# ─── Page de couverture (callback canvas) ───────────────────────────────────────
def draw_cover(canvas, doc):
    latest_dt = latest_results_timestamp()
    w, h = A4
    canvas.saveState()
    # Fond
    canvas.setFillColor(DARK_BLUE)
    canvas.rect(0, 0, w, h, fill=1, stroke=0)
    # Bande basse
    canvas.setFillColor(MID_BLUE)
    canvas.rect(0, 0, w, 3.0*cm, fill=1, stroke=0)
    # Accent horizontal
    canvas.setFillColor(ACCENT_BLUE)
    canvas.rect(0, 3.0*cm, w, 0.35*cm, fill=1, stroke=0)
    # Bande droite teal
    canvas.setFillColor(TEAL)
    canvas.rect(w - 1.0*cm, 0, 1.0*cm, h, fill=1, stroke=0)
    # Cercle
    cx = w / 2
    cy = h * 0.72
    canvas.setFillColor(colors.HexColor("#1565C0"))
    canvas.circle(cx, cy, 2.6*cm, fill=1, stroke=0)
    canvas.setStrokeColor(ACCENT_BLUE)
    canvas.setLineWidth(2)
    canvas.circle(cx, cy, 2.6*cm, fill=0, stroke=1)
    # "DT" dans le cercle
    canvas.setFillColor(WHITE)
    canvas.setFont(F_BOLD, 30)
    canvas.drawCentredString(cx, cy - 0.4*cm, "DT")
    canvas.setFont(F_BODY, 8)
    canvas.setFillColor(colors.HexColor("#BBDEFB"))
    canvas.drawCentredString(cx, cy + 1.4*cm, "HBDT-SumPath  |  CKKS  |  SEAL")
    # Titre
    canvas.setFillColor(WHITE)
    canvas.setFont(F_BOLD, 24)
    canvas.drawCentredString(cx, h * 0.50, "POC - Inference Privee sur")
    canvas.drawCentredString(cx, h * 0.50 - 0.9*cm, "Arbres de Decision")
    # Sous-titre
    canvas.setFont(F_BODY, 11.5)
    canvas.setFillColor(colors.HexColor("#BBDEFB"))
    canvas.drawCentredString(cx, h * 0.50 - 2.0*cm,
        "HBDT-SumPath  |  Approximation Polynomiale Adaptive  |  CKKS")
    # Ligne
    canvas.setStrokeColor(ACCENT_BLUE)
    canvas.setLineWidth(1)
    canvas.line(3.5*cm, h * 0.50 - 2.7*cm, w - 3.5*cm, h * 0.50 - 2.7*cm)
    # Nom rapport
    canvas.setFont(F_BOLD, 10.5)
    canvas.setFillColor(colors.HexColor("#90CAF9"))
    canvas.drawCentredString(cx, h * 0.50 - 3.4*cm, "My_POC_All  -  Rapport Technique Complet")
    # Auteur
    canvas.setFont(F_ITALIC, 10)
    canvas.setFillColor(colors.HexColor("#90CAF9"))
    canvas.drawCentredString(
        cx, 1.8*cm,
        f"Madicke Mbodj  |  Projet POC_DT  |  Resultats au {format_date_fr(latest_dt)}"
    )
    canvas.restoreState()

# ─── En-tete / Pied de page ──────────────────────────────────────────────────────
def on_page(canvas, doc):
    canvas.saveState()
    pn = canvas.getPageNumber()
    if pn == 1:
        draw_cover(canvas, doc)
        canvas.restoreState()
        return
    w, h = A4
    # En-tete
    canvas.setFillColor(DARK_BLUE)
    canvas.rect(0, h - 1.15*cm, w, 1.15*cm, fill=1, stroke=0)
    canvas.setFillColor(WHITE)
    canvas.setFont(F_BOLD, 7.5)
    canvas.drawString(1.5*cm, h - 0.72*cm,
        "POC - Inference Privee sur Arbres de Decision (HBDT-SumPath + CKKS)")
    canvas.setFont(F_BODY, 7.5)
    canvas.drawRightString(w - 1.5*cm, h - 0.72*cm, "Mbodj - My_POC_All")
    # Pied
    canvas.setFillColor(DARK_BLUE)
    canvas.rect(0, 0, w, 0.85*cm, fill=1, stroke=0)
    canvas.setFillColor(ACCENT_BLUE)
    canvas.rect(0, 0.85*cm, w, 0.12*cm, fill=1, stroke=0)
    canvas.setFillColor(WHITE)
    canvas.setFont(F_BODY, 8)
    canvas.drawCentredString(w / 2, 0.28*cm, f"Page {pn}")
    canvas.restoreState()

# ─── Graphiques ──────────────────────────────────────────────────────────────────
def chart_accuracy(datasets, clear_vals, he_vals, title=""):
    d = Drawing(13.5*cm, 7.5*cm)
    bc = VerticalBarChart()
    bc.x = 55; bc.y = 35
    bc.width = 13.5*cm - 75; bc.height = 7.5*cm - 55
    bc.data = [clear_vals, he_vals]
    bc.categoryAxis.categoryNames = datasets
    bc.categoryAxis.labels.angle = 25
    bc.categoryAxis.labels.fontSize = 8
    bc.categoryAxis.labels.fontName = F_BODY
    bc.categoryAxis.labels.dx = -4
    bc.valueAxis.valueMin = 0
    bc.valueAxis.valueMax = 110
    bc.valueAxis.valueStep = 20
    bc.valueAxis.labels.fontSize = 8
    bc.valueAxis.labels.fontName = F_BODY
    bc.bars[0].fillColor = LIGHT_BLUE
    bc.bars[1].fillColor = TEAL
    bc.groupSpacing = 10
    bc.barSpacing = 3
    bc.bars.strokeColor = None
    d.add(bc)
    # Legende
    lx, ly = bc.x, 7.5*cm - 18
    for i, (col, lab) in enumerate([(LIGHT_BLUE, "Clair (Hard)"), (TEAL, "Chiffre HE")]):
        d.add(Rect(lx + i*95, ly, 11, 9, fillColor=col, strokeColor=None))
        d.add(String(lx + i*95 + 15, ly + 1, lab,
                     fontSize=8, fontName=F_BODY, fillColor=GRAY_DARK))
    return d

def chart_timings(datasets, enc_t, inf_t, dec_t):
    d = Drawing(13.5*cm, 7.5*cm)
    bc = VerticalBarChart()
    bc.x = 60; bc.y = 35
    bc.width = 13.5*cm - 80; bc.height = 7.5*cm - 55
    max_v = max(inf_t) * 1.15
    step = 2000 if max_v > 5000 else 1000
    bc.data = [enc_t, inf_t, dec_t]
    bc.categoryAxis.categoryNames = datasets
    bc.categoryAxis.labels.angle = 20
    bc.categoryAxis.labels.fontSize = 7.5
    bc.categoryAxis.labels.fontName = F_BODY
    bc.valueAxis.valueMin = 0
    bc.valueAxis.valueMax = round(max_v / step + 1) * step
    bc.valueAxis.valueStep = step
    bc.valueAxis.labels.fontSize = 7.5
    bc.valueAxis.labels.fontName = F_BODY
    bc.bars[0].fillColor = ACCENT_BLUE
    bc.bars[1].fillColor = ORANGE
    bc.bars[2].fillColor = TEAL
    bc.groupSpacing = 10
    bc.barSpacing = 2
    bc.bars.strokeColor = None
    d.add(bc)
    lx, ly = bc.x, 7.5*cm - 18
    for i, (col, lab) in enumerate([
            (ACCENT_BLUE, "Chiffrement"),
            (ORANGE, "Inference serveur"),
            (TEAL, "Dechiffrement")]):
        d.add(Rect(lx + i*90, ly, 10, 9, fillColor=col, strokeColor=None))
        d.add(String(lx + i*90 + 14, ly + 1, lab,
                     fontSize=7.5, fontName=F_BODY, fillColor=GRAY_DARK))
    return d

# ══════════════════════════════════════════════════════════════════════════════════
#  CONTENU DU RAPPORT
# ══════════════════════════════════════════════════════════════════════════════════
def build_story():
    s = build_styles()
    story = []

    # ── PAGE 1 : couverture (dessinee en callback) ────────────────────────────────
    story.append(PageBreak())

    # ── TABLE DES MATIERES ────────────────────────────────────────────────────────
    story.append(h1("TABLE DES MATIERES", s))
    story.append(sp())
    toc = [
        ("1",  "Presentation generale du POC", 3),
        ("2",  "Architecture et structure du projet", 4),
        ("3",  "Pipeline d'inference - vue d'ensemble", 5),
        ("4",  "Module d'approximation polynomiale (SoftStepApprox)", 6),
        ("5",  "Mode Soft Adaptatif - algorithme et analyse de precision", 8),
        ("6",  "Chiffrement homomorphe CKKS - parametres et analyse", 10),
        ("7",  "Algorithme HBDT-SumPath et DataLayout", 13),
        ("8",  "Resultats experimentaux complets", 15),
        ("9",  "Analyse comparative : precision adaptative vs. precision HE", 20),
        ("10", "Limitations et cas d'echec observes", 22),
        ("11", "Conclusion", 23),
    ]
    for num, title, page in toc:
        row = [
            Paragraph(f"<b>{num}.</b>  {title}", s["toc_entry"]),
            Paragraph(str(page), s["toc_page"])
        ]
        t = Table([row], colWidths=[14.5*cm, 2.5*cm])
        t.setStyle(TableStyle([
            ("ALIGN",         (1, 0), (1, 0), "RIGHT"),
            ("VALIGN",        (0, 0), (-1, 0), "MIDDLE"),
            ("LINEBELOW",     (0, 0), (-1, 0), 0.3, GRAY_BORDER),
            ("TOPPADDING",    (0, 0), (-1, 0), 4),
            ("BOTTOMPADDING", (0, 0), (-1, 0), 4),
        ]))
        story.append(t)
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §1 PRESENTATION GENERALE
    # ══════════════════════════════════════════════
    story.append(h1("1  -  PRESENTATION GENERALE DU POC", s))
    story.append(sp())
    story.append(body(
        "Le projet <b>My_POC_All</b> est un demonstrateur complet d'<b>inference privee sur "
        "arbres de decision</b>. Il combine trois briques algorithmiques : "
        "(1) l'<b>approximation polynomiale adaptive</b> de la comparaison binaire, "
        "(2) l'algorithme <b>HBDT-SumPath</b> (Shin et al.) pour structurer l'inference "
        "dans les slots d'un chiffretexte CKKS, et (3) le chiffrement homomorphe "
        "<b>CKKS via Microsoft SEAL v4.1.2</b>. "
        "L'objectif est de permettre a un serveur d'evaluer un arbre de decision sur "
        "des donnees client chiffrees, sans jamais voir les donnees en clair.", s))
    story.append(sp(0.5))

    story.append(h2("1.1  Objectifs du POC", s))
    for item in [
        "Valider la faisabilite d'une inference homomorphe complete sur des jeux de donnees reels.",
        "Comparer les modes d'inference : <b>Hard</b> (seuil exact), <b>Soft global</b> "
        "(polynome uniforme), <b>Soft adaptatif</b> (degre par noeud selon le min_gap).",
        "Mesurer l'impact du choix du degre polynomial sur la precision, en clair et sous CKKS.",
        "Evaluer les temps de chiffrement, d'inference et de dechiffrement sur des datasets varies.",
        "Identifier les limites memoire et de profondeur multiplicative du schema CKKS.",
    ]:
        story.append(bitem(item, s))
    story.append(sp(0.5))

    story.append(h2("1.2  Datasets utilises", s))
    ds_data = [
        ["Dataset",  "Features", "Classes", "Echantillons test", "Precision arbre / dernier run clair"],
        ["Iris",     "4",  "3",  "50",  "100.00 % (run 20 samples)"],
        ["Cancer",   "30", "2",  "188", "100.00 % (soft adaptatif normalise, run 32 samples)"],
        ["Wine",     "13", "3",  "59",  "100.00 % (run 26 samples)"],
        ["Digits",   "64", "10", "1797", "100.00 % (run 32 samples)"],
        ["Breast",   "30", "2",  "86",  "90.62 % (soft adaptatif normalise, run 32 samples)"],
        ["Heart",    "13", "2",  "45",  "84.62 % (soft adaptatif normalise, run 26 samples)"],
        ["Steel",    "33", "2",  "292", "100.00 % (soft adaptatif normalise, run 32 samples)"],
        ["Spam",     "57", "2",  "691", "100.00 % (hard clair, run 2 samples ; HE OOM)"],
        ["Spam2",    "57", "2",  "691", "100.00 % (hard clair, run 2 samples ; HE non valide)"],
    ]
    story.append(make_table(ds_data,
        [2.5*cm, 2.0*cm, 2.0*cm, 3.5*cm, 7.0*cm], s))
    story.append(Paragraph(
        "Table 1 - Jeux de donnees utilises et precision de l'arbre de decision en clair.",
        s["caption"]))
    story.append(sp())
    story.append(note(
        "Les datasets Breast, Heart et Steel utilisent des arbres pre-entraines fournis par "
        "le projet SortingHat (Rust/C++ PDTE), dont la precision intrinsèque est limitee "
        "independamment de l'approximation polynomiale.", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §2 ARCHITECTURE
    # ══════════════════════════════════════════════
    story.append(h1("2  -  ARCHITECTURE ET STRUCTURE DU PROJET", s))
    story.append(sp())
    story.append(body(
        "Le projet est organise en modules C++17 compiles via CMake et executes dans un "
        "conteneur Podman (Ubuntu 22.04) pour garantir la reproductibilite. "
        "Un script Python <b>train_and_export.py</b> gere l'entrainement sklearn et l'export "
        "des arbres. Le script PowerShell <b>run.ps1</b> orchestre l'ensemble du pipeline.", s))
    story.append(sp(0.5))

    story.append(h2("2.1  Modules principaux C++", s))
    mods = [
        ["Module",          "Fichiers",              "Role"],
        ["TreeNode",        "TreeNode.h/cpp",        "Structure noeud (id, feature, threshold, min_gap, leaf_value)"],
        ["HardTree",        "HardTree.h/cpp",        "Inference hard exacte + collecte statistiques min_gap"],
        ["SoftStepApprox",  "SoftStepApprox.h/cpp",  "Approximation polynomiale de I0(t) par moindres carres pondere"],
        ["SoftTree",        "SoftTree.h/cpp",        "Inference soft en clair : modes GLOBAL et ADAPTIVE"],
        ["PolyEval",        "PolyEval.h/cpp",        "Algorithmes : Horner, BSGS, Clenshaw + metriques d'erreur"],
        ["DataLayout",      "DataLayout.h/cpp",      "Encodage one-hot, structure HBDT-SumPath, masques aleatoires"],
        ["OneHotEncoder",   "OneHotEncoder.h/cpp",   "Discretisation des features continues en one-hot"],
        ["ClearInference",  "ClearInference.h/cpp",  "Orchestration inference en clair (hard + soft global + adaptive)"],
        ["HEInference",     "HEInference.h/cpp",     "Inference chiffree CKKS : setup, chiffrement, evaluation, dechiffrement"],
        ["TreeExporter",    "TreeExporter.h/cpp",    "Import/export arbre : CSV, JSON, format Akavia, format sklearn"],
        ["Metrics",         "Metrics.h/cpp",         "Matrice de confusion, accuracy, F1, comparaison clair vs HE"],
    ]
    story.append(make_table(mods, [3.0*cm, 4.0*cm, 10.0*cm], s))
    story.append(Paragraph("Table 2 - Modules C++ du projet.", s["caption"]))
    story.append(sp())

    story.append(h2("2.2  Outils de build et execution", s))
    story.append(code(
        "# Execution complete (PowerShell)\n"
        ".\\run.ps1 -Dataset breast -SampleCount 32\n\n"
        "# Cibles CMake\n"
        "poc_clear           -- inference en clair (hard + soft global + soft adaptatif)\n"
        "poc_he              -- inference homomorphe CKKS (soft adaptatif)\n"
        "run_tests           -- tests unitaires (inference claire)\n"
        "run_tests_akavia    -- tests supplementaires HE\n\n"
        "# Dependances\n"
        "Microsoft SEAL v4.1.2  (compile depuis GitHub dans le conteneur)\n"
        "C++17  |  CMake >= 3.16  |  Python 3 + scikit-learn (entrainement)", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §3 PIPELINE
    # ══════════════════════════════════════════════
    story.append(h1("3  -  PIPELINE D'INFERENCE - VUE D'ENSEMBLE", s))
    story.append(sp())
    story.append(body(
        "Le pipeline complet comprend six etapes, de l'entrainement de l'arbre "
        "jusqu'a la recuperation du label predit sous chiffrement :", s))
    story.append(sp(0.5))

    pipeline_data = [
        ["Etape",             "Description",                                              "Intervenant"],
        ["1. Entrainement",   "train_and_export.py entraine un DecisionTreeClassifier "
                              "sklearn, calcule les min_gap par noeud via decision_path(), "
                              "exporte l'arbre en CSV + JSON.",                             "Python / sklearn"],
        ["2. Inference claire","poc_clear charge l'arbre, evalue les 3 modes (hard, "
                              "soft_global, soft_adaptive) sur n samples, affiche "
                              "accuracy + matrice de confusion.",                           "C++ (poc_clear)"],
        ["3. Setup CKKS",     "HEInference::setupCKKS() genere les cles (publique, secrete,"
                              " relin, galois), configure le contexte SEAL.",               "C++ (poc_he)"],
        ["4. Chiffrement",    "Le client encode l'input en one-hot, chiffre le vecteur "
                              "avec la cle publique. Le serveur ne voit que le chiffretexte.","Client"],
        ["5. Inference HE",   "Le serveur evalue phi(theta_i - x[feat]) pour chaque noeud "
                              "via BSGS polynomial + SumPath dans l'espace chiffre.",       "Serveur"],
        ["6. Dechiffrement",  "Le client dechiffre, identifie le slot de score minimal "
                              "(= 0 pour la bonne feuille), recupere le label predit.",     "Client"],
    ]
    story.append(make_table(pipeline_data, [3.2*cm, 10.5*cm, 3.3*cm], s))
    story.append(Paragraph("Table 3 - Etapes du pipeline d'inference privee.", s["caption"]))
    story.append(sp())
    story.append(note(
        "La propriete de confidentialite repose sur le fait que l'evaluation de l'arbre est "
        "entierement realisee dans l'espace chiffre. Le serveur n'apprend ni la valeur de l'input "
        "ni le label predit -- seul le client peut dechiffrer le resultat.", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §4 SOFTSTEPAPPROX
    # ══════════════════════════════════════════════
    story.append(h1("4  -  MODULE D'APPROXIMATION POLYNOMIALE (SoftStepApprox)", s))
    story.append(sp())
    story.append(body(
        "La cle algorithmique du POC est le remplacement de la comparaison binaire "
        "<b>1(x &lt;= theta)</b> -- non differentiable et incompatible avec HE -- par une "
        "approximation polynomiale lisse <b>phi(t)</b>, definie sur "
        "<b>t = theta - x[feature]</b>.", s))
    story.append(sp(0.5))

    story.append(h2("4.1  Probleme d'optimisation", s))
    story.append(code(
        "  Probleme de minimisation :\n"
        "  min_{p in Pn}  integral_{-2}^{2} (I0(x) - p(x))^2 * w(x) dx\n\n"
        "  ou  I0(x) = 1 si x >= 0,  = 0 sinon  (fonction echelon centree en 0)\n"
        "       w(x) = 0 si |x| < delta  (fenetre morte, delta = 0.05)\n"
        "             = 1 sinon\n\n"
        "  phi(t) = clamp(p(t), 0, 1)  -- stabilisation numerique\n\n"
        "  Convention HBDT :\n"
        "    left_i  = phi(theta_i - x[feat_i])  ~= 1 si x <= theta  (branche gauche)\n"
        "    I_i     = 1 - left_i                ~= 0 si x <= theta  (indicateur SumPath)", s))
    story.append(sp(0.5))

    story.append(h2("4.2  Resolution -- Moindres Carres Ponders par Vandermonde", s))
    story.append(body(
        "L'implementation resout le <b>systeme normal de Vandermonde</b> "
        "(V^T * V) * c = V^T * Wy sur n_pts = 2000 points uniformes sur [-2, 2]. "
        "La resolution utilise l'<b>elimination gaussienne avec pivot partiel</b> (Gauss-Jordan "
        "complet), sans bibliotheque externe -- coherent avec les contraintes d'un POC C++ standalone.", s))
    story.append(sp(0.5))

    story.append(h2("4.3  Degres disponibles et profondeur multiplicative BSGS", s))
    deg_data = [
        ["Degre d", "Baby-steps k", "Profondeur BSGS", "Usage typique"],
        ["4",  "2", "2 niveaux",  "Noeuds a grand gap (>= 0.08)"],
        ["8",  "3", "3 niveaux",  "Noeuds a gap modere (0.04 - 0.08) -- mode global par defaut"],
        ["16", "4", "4 niveaux",  "Noeuds a petit gap (0.015 - 0.04)"],
        ["32", "6", "5 niveaux",  "Noeuds a tres petit gap (< 0.015) -- cas les plus difficiles"],
    ]
    story.append(make_table(deg_data, [2.0*cm, 2.8*cm, 3.2*cm, 9.0*cm], s))
    story.append(Paragraph(
        "Table 4 - Degres polynomiaux et profondeurs multiplicatives (algorithme BSGS).",
        s["caption"]))
    story.append(sp(0.5))

    story.append(h2("4.4  Algorithme Baby-Step Giant-Step (BSGS)", s))
    story.append(code(
        "  k = ceil(sqrt(d))   -- baby steps\n"
        "  B = ceil(d / k)     -- giant steps\n\n"
        "  Baby-steps  : calcul de x, x^2, ..., x^{k-1}    (k-1 multiplications HE)\n"
        "  Giant-steps : T = x^k, T^2, ..., T^{B-1}        (log2(B) mult. square-and-multiply)\n\n"
        "  p(x) = somme_{j=0}^{B-1}  Q_j(x) * T^j\n"
        "  ou Q_j est un polynome de degre < k -- evalue par Horner sur les baby-steps.\n\n"
        "  Profondeur mult. : ~= log2(k) + log2(B) = O(log d)   vs. O(d) pour Horner.\n"
        "  Gain degre 32 : 5 niveaux au lieu de 32 (facteur 6x de reduction).", s))
    story.append(note(
        "En HE, chaque multiplication de chiffretexts consomme un niveau de la chaine de moduli. "
        "BSGS reduit cette consommation de O(d) a O(log d), permettant des polynomes de "
        "degre 32 avec seulement 5 niveaux au lieu de 32.", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §5 SOFT ADAPTATIF
    # ══════════════════════════════════════════════
    story.append(h1("5  -  MODE SOFT ADAPTATIF - ALGORITHME ET ANALYSE", s))
    story.append(sp())
    story.append(body(
        "Le mode <b>Soft Adaptatif</b> est l'innovation centrale du POC. Plutot qu'un degre "
        "polynomial uniforme pour tous les noeuds (mode global), chaque noeud se voit affecter "
        "un degre <b>adapte a sa difficulte de decision</b>, mesuree par le <b>min_gap</b>.", s))
    story.append(sp(0.5))

    story.append(h2("5.1  Definition du min_gap", s))
    story.append(body(
        "Pour un noeud interne <i>i</i> avec threshold <i>theta_i</i> et feature <i>f_i</i>, "
        "le <b>min_gap</b> est la <b>distance minimale</b> observee entre les valeurs "
        "de feature et le seuil, sur tous les echantillons d'entrainement qui passent par ce noeud :", s))
    story.append(code(
        "  min_gap(i) = min_{x in X : noeud i actif}  |x[feature_i] - theta_i|\n\n"
        "  Collecte dans HardTree::collectGapStats() via sklearn decision_path()\n"
        "  puis exporte dans la colonne 'min_gap' du fichier tree.csv.\n\n"
        "  Interpretation : un petit min_gap signifie que des samples d'entrainement\n"
        "  sont tres proches du seuil de decision -> noeud difficile a approximer.", s))
    story.append(sp(0.5))

    story.append(h2("5.2  Regle de selection adaptative du degre", s))
    story.append(code(
        "  // SoftStepApprox::chooseAdaptiveDegree(min_gap, tol=0.01, max_degree=32)\n\n"
        "  if (min_gap <= 0.0)   return max_degree;    // seuil inconnu -> max securite\n"
        "  if (min_gap >= 0.08)  return 4;             // grand gap  -> degre minimal\n"
        "  if (min_gap >= 0.04)  return 8;             // gap modere -> degre intermediaire\n"
        "  if (min_gap >= 0.015) return 16;            // petit gap  -> degre eleve\n"
        "  return max_degree;                          // tres petit -> degre maximum (32)", s))
    story.append(sp(0.5))

    story.append(h2("5.3  Statistiques de gap par dataset", s))
    gap_data = [
        ["Dataset", "Profondeur", "Noeuds", "min_gap global", "Median gap", "Degre dominant"],
        ["Iris",   "4", "13", "0.0169",  "0.0625", "16 (noeuds difficiles) / 4 (faciles)"],
        ["Cancer", "4", "25", "0.0015",  "0.0157", "32 (critique) / 16 (courant)"],
        ["Wine",   "4", "13", "0.0034",  "0.0253", "32 (noeuds serres) / 16 (mediants)"],
        ["Digits", "4", "15", "--",      "--",     "16 (profil standard)"],
        ["Breast", "~5","~31", "< 0.02", "~0.05",  "32 (majorite des noeuds)"],
        ["Heart",  "~4","~15", "< 0.03", "~0.06",  "16 - 32"],
        ["Steel",  "~5","~25", "< 0.02", "~0.04",  "16 - 32"],
    ]
    story.append(make_table(gap_data,
        [2.0*cm, 2.3*cm, 1.8*cm, 2.8*cm, 2.5*cm, 5.6*cm], s))
    story.append(Paragraph("Table 5 - Statistiques de gap par dataset.", s["caption"]))
    story.append(sp(0.5))

    story.append(h2("5.4  Impact de la precision sur le soft adaptatif (clair vs. hard)", s))
    story.append(body(
        "Comparaison de l'accuracy du mode hard (reference exacte) avec celle du "
        "mode soft adaptatif en clair, pour quantifier la perte due a l'approximation. "
        "Les valeurs ci-dessous viennent des derniers runs disponibles du POC principal :", s))
    prec_data = [["Dataset", "Hard (clair)", "Soft adapt. (clair)", "Delta accuracy",
                  "Soft global", "Interpretation"]]
    recent_soft = latest_rows_by_dataset(load_csv_rows(RUN_RESULTS_SOFT), preferred_key="clear_soft_adaptive_accuracy_pct")
    recent_soft_summary = summarize_poc_latest(recent_soft)
    for dataset in ["iris", "cancer", "wine", "digits", "breast", "heart", "steel", "spam", "spam2"]:
        row = recent_soft.get(dataset)
        if not row:
            continue
        hard_acc = parse_float(row.get("clear_hard_accuracy_pct"))
        adapt_acc = parse_float(row.get("clear_soft_adaptive_accuracy_pct"))
        global_acc = parse_float(row.get("clear_soft_global_accuracy_pct"))
        delta = "--"
        if hard_acc is not None and adapt_acc is not None:
            delta = f"{adapt_acc - hard_acc:+.2f} %"
        interpretation = (
            "Soft adaptatif aligne sur le global"
            if adapt_acc == global_acc else
            "Ecart visible entre global et adaptatif"
        )
        if dataset in ("breast", "heart", "steel"):
            interpretation = "Arbre source plus limitant que l'approximation seule"
        elif dataset in ("spam", "spam2"):
            interpretation = "Grande dimension ; validation HE encore limitee par la memoire"
        elif dataset in ("iris", "cancer", "wine", "digits"):
            interpretation = "Approximation moderee ; l'ecart vient surtout des noeuds proches du seuil"
        prec_data.append([
            dataset.capitalize(),
            fmt_pct(row.get("clear_hard_accuracy_pct")),
            fmt_pct(row.get("clear_soft_adaptive_accuracy_pct")),
            delta,
            fmt_pct(row.get("clear_soft_global_accuracy_pct")),
            interpretation,
        ])
    story.append(make_table(prec_data,
        [1.9*cm, 2.4*cm, 2.9*cm, 2.2*cm, 2.3*cm, 5.3*cm], s))
    story.append(Paragraph(
        "Table 6 - Impact precision soft adaptatif : comparaison hard vs. soft adaptatif en clair.",
        s["caption"]))
    story.append(sp(0.5))
    worst_soft_gap = recent_soft_summary["worst_soft_gap"]
    story.append(note(
        "Les resultats recents montrent une image plus nuancee que l'hypothese initiale : "
        f"sur {recent_soft_summary['count']} datasets disponibles, l'ecart moyen entre hard et "
        f"soft adaptatif est de {fmt_pct(recent_soft_summary['avg_hard_gap'])}. "
        f"L'ecart maximal observe est sur {display_dataset_name(worst_soft_gap[0])} "
        f"avec {fmt_delta_pct(-worst_soft_gap[1])} par rapport au hard. "
        "Le mode adaptatif reste donc coherent, mais il n'est ni systematiquement egal au hard "
        "ni automatiquement meilleur que le soft global. Les seuils {0.08, 0.04, 0.015} "
        "constituent une base raisonnable, plutot qu'une garantie d'optimalite.", s))
    story.append(body(
        "Ce resultat est <b>attendu theoriquement</b> : la fenetre morte delta = 0.05 garantit "
        "que pour tout echantillon a distance > delta du seuil, phi(t) est saturee a 0 ou 1 "
        "(identique a I0). La probabilite d'erreur due a l'approximation est nulle en dehors "
        "de la fenetre [-delta, delta]. En pratique, les ecarts observes confirment surtout que "
        "les samples proches du seuil, la qualite de l'arbre source et la calibration du degre "
        "restent les trois facteurs dominants.", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §6 CKKS
    # ══════════════════════════════════════════════
    story.append(h1("6  -  CHIFFREMENT HOMOMORPHE CKKS - PARAMETRES ET ANALYSE", s))
    story.append(sp())
    story.append(body(
        "Le schema CKKS (Cheon-Kim-Kim-Song, 2017) est un schema de chiffrement homomorphe "
        "approxime, adapte aux calculs sur des nombres reels. Il permet des additions et "
        "multiplications sur des chiffretexts avec une perte de precision controlee. "
        "La bibliotheque <b>Microsoft SEAL v4.1.2</b> fournit l'implementation de reference.", s))
    story.append(sp(0.5))

    story.append(h2("6.1  Parametres CKKS configures dans le POC", s))
    ckks_data = [
        ["Parametre",               "Valeur",                  "Impact"],
        ["polyModulusDegree (N)",   "2^14 = 16 384",
         "Degre polynomial RNS. Determine N/2 = 8192 slots SIMD et le niveau de securite (~128 bits)."],
        ["scale (Delta)",           "2^40",
         "Facteur d'echelle : les reels x sont encodes comme floor(x * 2^40). Precision : ~12 decimales."],
        ["scaleModSize",            "50 bits (config) / 40 bits (effectif)",
         "Taille des moduli RNS. Controle la precision residuelle apres rescaling."],
        ["multDepth",               "12",
         "Budget de multiplications en profondeur. Chaque multiplication CKKS consomme 1 niveau."],
        ["Coefficient modulus",     "Chaine ~13 moduli 40/50-bit",
         "Total ~500 bits. Determine la capacite de traitement homomorphe."],
        ["Slot capacity",           "8 192 (SIMD)",
         "Nombre de reels encodes dans un seul chiffretexte. Permet le batching."],
        ["Galois keys",             "Generees pour rotations",
         "Permettent les rotations de slots necessaires a SumPath (log2(N/2) rotations)."],
        ["Relin keys",              "Generees post-multiplication",
         "Reduction de taille du chiffretexte apres multiplication ciphertext x ciphertext."],
    ]
    story.append(make_table(ckks_data, [3.8*cm, 4.2*cm, 9.0*cm], s))
    story.append(Paragraph("Table 7 - Parametres CKKS dans HEInference::setupCKKS().", s["caption"]))
    story.append(sp(0.5))

    story.append(h2("6.2  Modele de bruit CKKS et precision residuelle", s))
    story.append(code(
        "  Erreur CKKS apres L multiplications :\n"
        "  epsilon_total ~= N * sqrt(L) * (q_L / Delta)  +  epsilon_arrondi * L\n\n"
        "  ou  q_L  = module residuel apres L rescalings\n"
        "       Delta = facteur d'echelle = 2^40\n"
        "       N    = degre polynomial = 16384\n\n"
        "  Pour L = 6 niveaux effectifs (polynome degre 8 via BSGS + SumPath) :\n"
        "    epsilon_total ~= 10^-6 a 10^-5\n"
        "    Seuil de decision delta = 0.05\n"
        "    Ratio bruit/seuil ~= 10^-3 a 10^-4  (tres securise)", s))
    story.append(sp(0.5))

    story.append(h2("6.3  Impact de la precision CKKS sur la classification", s))
    story.append(body(
        "La precision CKKS affecte la classification uniquement si l'erreur numerique fait "
        "basculer phi(t) de part et d'autre d'un seuil de decision. "
        "On identifie trois regimes :", s))
    for item in [
        "<b>Regime 1 -- Noeud a grand gap (>= 0.08) :</b> phi(t) est saturee (proche de 0 ou 1). "
        "L'erreur CKKS (~10^-5) est negligeable. Aucun basculement possible.",
        "<b>Regime 2 -- Noeud a gap intermediaire (0.015 - 0.08) :</b> phi(t) est entre 0 et 1 "
        "mais loin de 0.5. Le bruit CKKS peut introduire une legere imprecision, "
        "mais SumPath accumule les indicateurs -- la bonne feuille reste celle avec la somme minimale.",
        "<b>Regime 3 -- Noeud a tres petit gap (< 0.015) :</b> phi(t) ~= 0.5 (ambiguite). "
        "Le bruit CKKS peut faire basculer la decision. Dans les derniers runs, ce cas est "
        "surtout visible sur Cancer (min_gap = 0.0015) avec une degradation HE d'environ 3 %, "
        "alors que Wine reste stable en adaptatif apres normalisation.",
    ]:
        story.append(bitem(item, s))
    story.append(sp(0.5))

    story.append(h2("6.4  Tableau comparatif : precision HE vs. precision clair", s))
    he_prec_data = [
        ["Dataset", "Soft adapt.\n(clair)", "HE adapt.", "Delta HE",
         "min_gap", "Cause de la degradation"],
        ["Iris",   "95.00 %", "95.00 %",  "  0.00 %",  "0.0169",
         "Petit batch ; aucun ecart HE supplementaire par rapport au clair adaptatif"],
        ["Cancer", "100.00 %", "96.88 %",  "-3.12 %",  "0.0015",
         "min_gap tres faible -> bruit CKKS franchit le seuil sur ~1 noeud critique"],
        ["Wine",   "100.00 %", "100.00 %",  "  0.00 %",  "0.0034",
         "Gap faible mais adaptation suffisamment stable dans le run courant"],
        ["Digits", "96.88 %", "96.88 %",  "  0.00 %",  "--",
         "Le mode adaptatif HE retrouve exactement le clair adaptatif sur ce batch"],
        ["Breast", "90.62 %", "90.62 %",  "  0.00 %", "< 0.02",
         "Normalisation par noeud des logits -> coherence clair/HE restauree"],
        ["Heart",  "84.62 %", "84.62 %",  "  0.00 %", "< 0.03",
         "Normalisation par noeud des logits -> le mode adaptatif retrouve le hard"],
        ["Steel",  "100.00 %", "100.00 %",  "  0.00 %", "< 0.02",
         "Normalisation par noeud des logits -> inference parfaite sur 32 samples"],
    ]
    story.append(make_table(he_prec_data,
        [1.9*cm, 2.5*cm, 2.3*cm, 2.0*cm, 1.8*cm, 6.5*cm], s))
    story.append(Paragraph(
        "Table 8 - Precision clair (soft adaptatif) vs. precision HE. "
        "Les degradations HE residuelles restent concentrees sur les tres petits min_gap.", s["caption"]))
    story.append(sp(0.5))

    story.append(h2("6.5  Packing SIMD et batching vertical", s))
    story.append(body(
        "L'implementation utilise le <b>vertical packing</b> pour evaluer plusieurs samples "
        "en parallele dans un seul chiffretexte. Avec N/2 = 8192 slots disponibles :", s))
    story.append(code(
        "  slots par sample = n_features * n_max\n"
        "  packed_samples   = floor(N/2 / slots_par_sample)\n\n"
        "  Exemple breast (30 features, n_max = 8) :\n"
        "    slots/sample = 30 * 8 = 240\n"
        "    packed_samples = 8192 / 240 ~= 34  ->  batch effectif = 32\n\n"
        "  Exemple spam (57 features, n_max = 8) :\n"
        "    slots/sample = 57 * 8 = 456\n"
        "    packed_samples = 8192 / 456 ~= 17  (faisable mais besoin de plus de memoire)\n\n"
        "  Gain : temps serveur divise par packed_samples (inférence SIMD).", s))
    story.append(sp(0.5))
    story.append(note(
        "Le packing vertical n'affecte pas la precision par sample (CKKS est SIMD exact). "
        "Seules les rotations (galois keys) introduisent une legere surcharge memoire.", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §7 HBDT-SUMPATH
    # ══════════════════════════════════════════════
    story.append(h1("7  -  ALGORITHME HBDT-SUMPATH ET DATALAYOUT", s))
    story.append(sp())
    story.append(body(
        "L'algorithme <b>HBDT-SumPath</b> (Shin et al.) reduit la <b>profondeur "
        "multiplicative</b> de l'inference a <b>O(1)</b> (independamment de la profondeur "
        "de l'arbre D), en exploitant le packing SIMD des chiffretexts CKKS.", s))
    story.append(sp(0.5))

    story.append(h2("7.1  Principe de l'algorithme SumPath", s))
    story.append(code(
        "  Pour chaque noeud interne Ni (i = 1..M) :\n"
        "    I_i = 1 - phi(theta_i - x[feat_i])  in {0, 1}\n"
        "    (0 = condition satisfaite, branche gauche prise)\n\n"
        "  Pour chaque feuille f_k, le chemin racine->f_k passe\n"
        "  par les noeuds {N_pi(k,1), ..., N_pi(k,D)} :\n"
        "    Score(f_k) = somme_{j=1}^D  I_pi(k,j)\n"
        "    -> La feuille correcte a Score = 0 (tous les noeuds du chemin satisfaits)\n"
        "    -> Les autres feuilles ont Score >= 1\n\n"
        "  Apres multiplication par masque aleatoire + rotation :\n"
        "    seul le slot de la feuille correcte est non-nul (mais obfusque).", s))
    story.append(sp(0.5))

    story.append(h2("7.2  Structure DataLayout et encodage one-hot", s))
    story.append(body(
        "Chaque noeud est mappe a un <b>bloc de slots</b> dans le chiffretexte. "
        "Le vecteur d'entree x est <b>encode en one-hot discretise</b> sur n_max = 8 valeurs :", s))
    story.append(code(
        "  Bloc Ni = [0, ..., 0, x[feat_i], 0, ..., 0]  (taille n_max * n_features)\n"
        "           ^^-- seul le bin correspondant est actif\n\n"
        "  Taille totale M = n_nodes * n_max * n_features  (arrondi puissance de 2)\n\n"
        "  Avantage : le calcul de phi(theta_i - x[feat_i]) pour TOUS les noeuds\n"
        "  se reduit a UNE SEULE evaluation polynomiale sur le chiffretexte encode\n"
        "  suivi de rotations pour aligner les blocs.", s))
    story.append(sp(0.5))

    story.append(h2("7.3  Masquage et obfuscation (confidentialite)", s))
    for item in [
        "Des masques aleatoires <b>r_k</b> sont pre-generes pour chaque feuille "
        "(seed = 42 pour la reproductibilite dans le POC).",
        "Apres SumPath, le resultat est multiplie par le masque : le serveur ne peut "
        "pas distinguer quelle feuille a ete activee.",
        "Une rotation aleatoire supplementaire obfusque la position du slot actif.",
        "Le client dechiffre, identifie le slot actif (score ~= 0 apres demasquage), "
        "et decode le label de la feuille correspondante.",
    ]:
        story.append(bitem(item, s))
    story.append(sp())
    story.append(note(
        "La propriete HBDT-SumPath O(1) en profondeur multiplicative est valable car "
        "la phase SumPath n'utilise que des additions et des rotations (sans multiplications "
        "entre indicateurs de noeuds differents). Seule l'evaluation polynomiale phi "
        "consomme des niveaux multiplicatifs.", s))
    story.append(sp(0.5))

    story.append(h2("7.4  Pseudo-codes essentiels du POC", s))
    story.append(body(
        "Les pseudo-codes suivants resument les morceaux les plus importants du POC tel qu'il "
        "est implemente dans <b>TreeExporter.cpp</b>, <b>DataLayout.cpp</b>, "
        "<b>ClearInference.cpp</b> et <b>HEInference.cpp</b>. L'objectif est de donner une "
        "lecture rapide du coeur algorithmique sans noyer le lecteur dans les details de "
        "plomberie C++/SEAL.", s))
    story.append(sp(0.5))

    story.append(h3("Pseudo-code 1 - Chargement d'un arbre deja entraine", s))
    story.append(code(
        "ENTREE : chemin vers un modele deja entraine (CSV, JSON, Akavia)\n"
        "SORTIE : arbre binaire interne TreeNode\n\n"
        "1. Detecter le format du fichier modele\n"
        "2. Si format CSV :\n"
        "     lire chaque ligne = un noeud\n"
        "     recuperer node_id, feature, threshold, left_id, right_id,\n"
        "     class_label, min_gap\n"
        "3. Si format JSON :\n"
        "     parser recursivement les champs left/right/is_leaf\n"
        "4. Reconstruire les pointeurs gauche/droite entre noeuds\n"
        "5. Assigner profondeur, ids et metadonnees utiles\n"
        "6. Produire la racine root du modele\n"
        "7. Charger ensuite X_test et y_test pour l'evaluation", s))
    story.append(sp(0.5))

    story.append(h3("Pseudo-code 2 - Inference claire complete", s))
    story.append(code(
        "ENTREE : sample x, arbre hard, DataLayout\n"
        "SORTIE : pred_hard, pred_soft_global, pred_soft_adaptive\n\n"
        "1. pred_hard <- parcourir l'arbre avec les seuils exacts\n"
        "2. Pour soft_global :\n"
        "     utiliser le meme degre polynomial pour tous les noeuds\n"
        "     pred_soft_global <- DataLayout.predict(x, use_soft = true)\n"
        "3. Pour soft_adaptive :\n"
        "     pour chaque noeud i, choisir degree_i a partir de min_gap_i\n"
        "     pred_soft_adaptive <- DataLayout.predict(x, use_soft = true,\n"
        "                                             poly_degrees = {degree_i})\n"
        "4. Comparer les trois labels avec y_true\n"
        "5. Agreger accuracy, matrices de confusion et statistiques", s))
    story.append(sp(0.5))

    story.append(h3("Pseudo-code 3 - Coeur HBDT-SumPath", s))
    story.append(code(
        "FONCTION PredictSumPath(x, use_soft, poly_degrees)\n"
        "1. Pour chaque noeud interne i :\n"
        "     si mode hard :\n"
        "        I_i <- 0 si x[feat_i] <= theta_i, sinon 1\n"
        "     sinon :\n"
        "        left_i <- phi(theta_i - x[feat_i], degree_i)\n"
        "        I_i <- 1 - left_i\n"
        "2. Pour chaque feuille f_k :\n"
        "     score_k <- 0\n"
        "     pour chaque etape du chemin racine -> f_k :\n"
        "        si branche gauche : score_k <- score_k + I_i\n"
        "        sinon            : score_k <- score_k + (1 - I_i)\n"
        "3. Choisir la feuille dont le score est minimal\n"
        "4. Retourner class_label(feuille_min)\n\n"
        "IDEe CLE : la bonne feuille a un score proche de 0.", s))
    story.append(sp(0.5))

    story.append(h3("Pseudo-code 4 - Inference homomorphe CKKS client/serveur", s))
    story.append(code(
        "COTE CLIENT\n"
        "1. Encoder le sample x dans les slots CKKS\n"
        "2. Chiffrer x -> ct_input\n"
        "3. Envoyer ct_input au serveur\n\n"
        "COTE SERVEUR\n"
        "4. Pour chaque noeud i :\n"
        "     degree_i <- degree global ou degree adaptatif\n"
        "     ct_I_i <- evaluation homomorphe de phi(theta_i - x[feat_i])\n"
        "5. Calculer ct_path_scores avec SumPath (additions + rotations)\n"
        "6. Masquer / aligner les slots correspondant aux feuilles\n"
        "7. Construire ct_result et le renvoyer au client\n\n"
        "COTE CLIENT\n"
        "8. Dechiffrer ct_result\n"
        "9. Lire les scores de feuilles\n"
        "10. Selectionner la feuille de score minimal\n"
        "11. Retourner le label final", s))
    story.append(sp(0.5))
    story.append(note(
        "Ces pseudo-codes montrent bien la separation conceptuelle du POC : "
        "le modele est d'abord importe depuis un arbre deja entraine, puis l'approximation "
        "polynomiale remplace la comparaison binaire, HBDT-SumPath evite "
        "la multiplication le long de tous les chemins, et CKKS permet d'executer cette "
        "logique sur des donnees chiffrees.", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §8 RESULTATS
    # ══════════════════════════════════════════════
    story.append(h1("8  -  RESULTATS EXPERIMENTAUX COMPLETS", s))
    story.append(sp())

    soft_rows = load_csv_rows(RUN_RESULTS_SOFT)
    soft_latest = latest_rows_by_dataset(soft_rows, preferred_key="he_soft_adaptive_accuracy_pct")
    soft_latest.pop("digits", None)
    soft_summary = summarize_poc_latest(soft_latest)
    akavia_rows = load_csv_rows(RUN_RESULTS_AKAVIA)
    akavia_latest = latest_rows_by_dataset(akavia_rows, preferred_key="he_batch_precision_pct")
    sortinghat_rows = load_csv_rows(RUN_RESULTS_SORTINGHAT)
    sortinghat_latest = latest_rows_by_dataset(sortinghat_rows, preferred_key="he_batch_precision_pct")

    story.append(h2("8.1  Nouveaux resultats du POC (hard, soft global, soft adaptatif)", s))
    story.append(body(
        "Cette section synthétise les derniers runs du POC principal a partir du fichier "
        "run_results_soft_adaptatif.csv. Les lignes ci-dessous proviennent des executions "
        "les plus recentes disponibles par dataset et incluent maintenant les trois references "
        "en clair (hard, soft global, soft adaptatif) ainsi que les deux variantes HE.", s))
    poc_data = [[
        "Dataset", "Feat.", "Cl.",
        "Hard clair", "Soft global", "Soft adapt. clair",
        "HE soft global", "HE soft adapt.", "HE sg. (ms)", "HE sa. (ms)"
    ]]
    preferred_poc_order = ["iris", "cancer", "wine", "digits", "breast", "heart", "steel", "spam", "spam2"]
    poc_order = [d for d in preferred_poc_order if d in soft_latest] + [d for d in soft_latest if d not in preferred_poc_order]
    for dataset in poc_order:
        row = soft_latest.get(dataset)
        if not row:
            continue
        poc_data.append([
            display_dataset_name(dataset),
            row.get("features", "--"),
            row.get("classes", "--"),
            fmt_pct(row.get("clear_hard_accuracy_pct")),
            fmt_pct(row.get("clear_soft_global_accuracy_pct")),
            fmt_pct(row.get("clear_soft_adaptive_accuracy_pct")),
            fmt_pct(row.get("he_soft_global_accuracy_pct")),
            fmt_pct(row.get("he_soft_adaptive_accuracy_pct")),
            fmt_ms(row.get("he_soft_global_avg_ms")),
            fmt_ms(row.get("he_soft_adaptive_avg_ms")),
        ])
    story.append(make_table(poc_data,
        [1.8*cm, 1.1*cm, 1.0*cm, 2.0*cm, 2.1*cm, 2.3*cm, 2.2*cm, 2.2*cm, 2.0*cm, 2.0*cm], s))
    story.append(Paragraph(
        "Table 9 - Derniers resultats du POC principal, avec comparaison complete entre hard, "
        "soft global, soft adaptatif et leurs equivalents CKKS.",
        s["caption"]))
    story.append(sp(0.5))
    worst_soft_gap = soft_summary["worst_soft_gap"]
    worst_he_gap = soft_summary["worst_he_gap"]
    he_match_names = ", ".join(display_dataset_name(name) for name in soft_summary["he_matches_clear"])
    he_gain_names = ", ".join(display_dataset_name(name) for name in soft_summary["he_beats_global"])
    story.append(note(
        f"Le POC principal couvre actuellement {soft_summary['count']} datasets. "
        f"L'ecart moyen entre hard clair et soft adaptatif clair est de {fmt_pct(soft_summary['avg_hard_gap'])}, "
        f"avec un maximum sur {display_dataset_name(worst_soft_gap[0])} ({fmt_delta_pct(-worst_soft_gap[1])}). "
        f"Cote HE, l'ecart moyen entre soft adaptatif clair et sa version chiffree est de {fmt_pct(soft_summary['avg_he_gap'])} ; "
        f"l'ecart maximal apparait sur {display_dataset_name(worst_he_gap[0])} ({fmt_delta_pct(-worst_he_gap[1])}). "
        f"Les sorties HE reproduisent exactement le soft adaptatif clair sur : {he_match_names or '--'}. "
        f"Le soft adaptatif HE depasse le soft global HE sur : {he_gain_names or 'aucun dataset'}.",
        s))

    story.append(h2("8.2  Resultats du projet Akavia (run_results.csv)", s))
    if akavia_rows:
        story.append(body(
            "Le fichier historique run_results.csv correspond aux experiments de reference du projet "
            "Akavia/seal-compare. On conserve ici le dernier run disponible par dataset afin de comparer "
            "les performances HE de cette baseline avec le POC actuel.", s))
        akavia_data = [[
            "Dataset", "Feat.", "Cl.", "Hard clair", "Batch HE",
            "Precision HE", "Setup", "Inference HE", "Tps/sample"
        ]]
        for dataset in ["iris", "cancer", "wine"]:
            row = akavia_latest.get(dataset)
            if not row:
                continue
            akavia_data.append([
                dataset.capitalize(),
                row.get("features", "--"),
                row.get("classes", "--"),
                fmt_pct(row.get("clear_hard_baseline_pct")),
                row.get("he_batch_tests", "--"),
                fmt_pct(row.get("he_batch_precision_pct")),
                fmt_ms(row.get("he_setup_time_ms")),
                fmt_ms(row.get("he_inference_time_ms")),
                fmt_ms(row.get("he_inference_time_per_sample_ms")),
            ])
        story.append(make_table(akavia_data,
            [1.8*cm, 1.1*cm, 1.0*cm, 2.2*cm, 1.6*cm, 2.1*cm, 2.0*cm, 2.5*cm, 2.2*cm], s))
        story.append(Paragraph(
            "Table 10 - Synthese des derniers resultats Akavia disponibles localement.",
            s["caption"]))
    else:
        story.append(warn(
            f"Aucun fichier Akavia (`run_results.csv`) n'est disponible localement au "
            f"{format_date_fr(latest_results_timestamp())}. Cette comparaison reste donc vide dans ce workspace.",
            s))
    story.append(sp(0.5))

    story.append(h2("8.3  Resultats SortingHat / autres projets", s))
    if sortinghat_rows:
        story.append(body(
            "Les fichiers run_results_sortinghat_transciphering.csv regroupent les evaluations menees "
            "sur les variantes SortingHat. La table garde le dernier etat connu par dataset, y compris "
            "les echecs OOM. Concrete-ML est mentionne comme piste dans le projet, mais aucun fichier de "
            f"mesures Concrete-ML n'est present dans ce workspace au {format_date_fr(latest_results_timestamp())}.", s))
        sh_data = [["Dataset", "Feat.", "Batch", "Statut", "Precision HE", "Tps/sample"]]
        sh_order = [
            "Sortinghat_transciphering/breast_11bits",
            "Sortinghat_transciphering/electricity_10bits",
            "Sortinghat_transciphering/phoneme_10bits",
            "Sortinghat_transciphering/spam_11bits",
            "Sortinghat_rust_pdte/breast",
            "Sortinghat_rust_pdte/heart",
            "Sortinghat_rust_pdte/steel",
            "Sortinghat_rust_pdte/spam",
            "Sortinghat_rust_pdte/spam2",
        ]
        for dataset in sh_order:
            row = sortinghat_latest.get(dataset)
            if not row:
                continue
            sh_data.append([
                dataset,
                row.get("features", "--"),
                row.get("he_batch_tests") or row.get("samples") or "--",
                fmt_status_from_row(row),
                fmt_pct(row.get("he_batch_precision_pct")),
                fmt_ms(row.get("he_inference_time_per_sample_ms")),
            ])
        story.append(make_table(sh_data,
            [6.0*cm, 1.1*cm, 1.4*cm, 1.8*cm, 2.1*cm, 2.2*cm], s))
        story.append(Paragraph(
            "Table 11 - Etat des experiments SortingHat disponibles localement. Les echecs sont "
            "majoritairement lies a des OOM sur les datasets les plus lourds.",
            s["caption"]))
    else:
        story.append(warn(
            f"Aucun fichier SortingHat (`run_results_sortinghat_transciphering.csv`) n'est disponible localement "
            f"au {format_date_fr(latest_results_timestamp())}.", s))
    story.append(sp(0.5))

    # Graphiques
    story.append(h2("8.4  Graphique : hard clair vs. HE soft adaptatif (POC principal)", s))
    chart_rows = [soft_latest[d] for d in poc_order if d in soft_latest and parse_float(soft_latest[d].get("he_soft_adaptive_accuracy_pct")) is not None]
    datasets_chart = [row["dataset"].capitalize() for row in chart_rows]
    acc_clear = [parse_float(row.get("clear_hard_accuracy_pct")) for row in chart_rows]
    acc_he = [parse_float(row.get("he_soft_adaptive_accuracy_pct")) for row in chart_rows]
    story.append(chart_accuracy(datasets_chart, acc_clear, acc_he))
    story.append(Paragraph(
        "Figure 1 - Comparaison entre la reference hard en clair et la precision CKKS du "
        "soft adaptatif sur les derniers runs du POC principal.",
        s["caption"]))
    story.append(sp(0.5))

    story.append(h2("8.5  Graphique : Temps HE moyens par dataset (soft adaptatif)", s))
    datasets_t = [row["dataset"].capitalize() for row in chart_rows]
    enc_t = [0.0 for _ in chart_rows]
    inf_t = [parse_float(row.get("he_soft_adaptive_avg_ms")) or 0.0 for row in chart_rows]
    dec_t = [0.0 for _ in chart_rows]
    story.append(chart_timings(datasets_t, enc_t, inf_t, dec_t))
    story.append(Paragraph(
        "Figure 2 - Temps moyen par sample pour le soft adaptatif homomorphe sur les derniers "
        "runs du POC principal. Les composantes chiffrement/dechiffrement ne sont pas encore "
        "exportees dans run_results_soft_adaptatif.csv, d'ou leur affichage nul ici.",
        s["caption"]))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §9 ANALYSE COMPARATIVE
    # ══════════════════════════════════════════════
    story.append(h1("9  -  ANALYSE COMPARATIVE : PRECISION ADAPTATIVE VS. PRECISION HE", s))
    story.append(sp())

    story.append(h2("9.1  Facteurs influencant la precision du soft adaptatif", s))
    story.append(body(
        "Plusieurs facteurs gouvernent la precision du mode soft adaptatif, "
        "independamment du bruit HE :", s))
    fa_data = [
        ["Facteur",                   "Impact",                                        "Datasets concernes"],
        ["Normalisation des logits",  "Sur les arbres pre-entraines multi-echelle, une "
                                      "normalisation locale |x-theta| par noeud peut supprimer "
                                      "l'essentiel du gap soft adaptatif.",           "Breast, Heart, Steel"],
        ["Valeur du min_gap",         "Un min_gap tres faible (< 0.005) signifie des "
                                      "samples proches du seuil. La precision depend "
                                      "fortement du degre polynomial (32 necessaire).", "Cancer, Wine"],
        ["Heuristique adaptive",      "Le choix du degre via min_gap reste une heuristique. "
                                      "Sur certains jeux, soft global peut egaler ou depasser "
                                      "legerement soft adaptatif si la calibration locale n'est "
                                      "pas ideale.",                                    "Iris, Cancer, Wine"],
        ["Fenetre morte delta = 0.05","Samples avec |x - theta| < 0.05 sont dans la "
                                      "zone d'incertitude. Une meme fenetre peut etre adequate "
                                      "pour un dataset et trop grossiere pour un autre.",       "Tous"],
        ["Profondeur de l'arbre",     "Un arbre plus profond cumule plus d'erreurs. "
                                      "Avec D = 4 niveaux, la degradation reste "
                                      "marginale (< 5 %).",                             "Tous (D = 4)"],
    ]
    story.append(make_table(fa_data, [3.5*cm, 8.5*cm, 5.0*cm], s))
    story.append(Paragraph("Table 12 - Facteurs influencant la precision du soft adaptatif.", s["caption"]))
    story.append(sp(0.5))

    story.append(h2("9.2  Facteurs influencant la precision HE", s))
    he_fa_data = [
        ["Facteur CKKS",              "Impact observe",                                 "Valeur dans le POC"],
        ["Bruit de rescaling",        "Principal contributeur a la degradation HE. "
                                      "Chaque multiplication ajoute ~2^-40 * N "
                                      "d'erreur. Pour 6 niveaux : ~10^-5.",             "multDepth=12 -> 6 effectifs"],
        ["Facteur d'echelle Delta",   "Delta = 2^40 donne 12 decimales de precision. "
                                      "Pour min_gap < 0.005 (Cancer, Wine), "
                                      "Delta = 2^50 serait plus sur.",                  "2^40 -- suffisant pour delta=0.05"],
        ["Degre polynomial phi",      "Degre 32 consomme 5 niveaux (BSGS). "
                                      "Precision residuelle legerement inferieure "
                                      "a degre 8 (3 niveaux) mais reste adequate.",     "Degre 32 -> 5 niveaux; degre 8 -> 3"],
        ["Packing SIMD",              "N'affecte pas la precision par sample "
                                      "(CKKS est SIMD exact). Seules les rotations "
                                      "galois introduisent une legere surcharge.",       "32 samples packes"],
        ["Alignement de moduli",      "Additions de chiffretexts de niveaux differents "
                                      "necessitent des rescalings supplementaires. "
                                      "Gere par alignCiphertexts().",                   "Implemente automatiquement"],
    ]
    story.append(make_table(he_fa_data, [3.5*cm, 8.0*cm, 5.5*cm], s))
    story.append(Paragraph("Table 13 - Facteurs influencant la precision HE.", s["caption"]))
    story.append(sp(0.5))

    story.append(h2("9.3  Modele predictif par regime de gap", s))
    story.append(code(
        "  Regime 1 : gap confortable et arbre stable  ->  HE proche du soft clair\n"
        "    Erreur CKKS << marge de decision  ->  peu de basculements\n"
        "    Exemples : Iris, Cancer, Wine sur les derniers runs du POC\n\n"
        "  Regime 2 : gap intermediaire ou logits mal calibres  ->  ecart possible\n"
        "    Une normalisation locale par noeud peut suffire a restaurer la precision\n"
        "    Exemples : Breast, Heart, Steel apres correction des logits\n\n"
        "  Regime 3 : min_gap < 0.015  ->  Degradation moderee (3 - 5 %)\n"
        "    Datasets : Cancer (0.0015), Wine (0.0034)\n"
        "    Degradation HE observee = 3 - 4 %   [CONFIRME]\n\n"
        "  Recommandation :\n"
        "    Si min_gap < 0.005 -> augmenter Delta a 2^50 ou max_degree a 64.", s))
    story.append(sp(0.5))

    story.append(h2("9.4  Synthese : precision vs. cout computationnel", s))
    synt_data = [
        ["Mode",             "Precision typique",      "Temps typique",
         "Confidentialite",  "Avantages",              "Inconvenients"],
        ["Hard (clair)",      "Reference (100 %)",     "< 1 ms/sample",
         "Aucune",           "Exact, rapide",          "Aucune protection des donnees"],
        ["Soft global (clair)","~= Hard (max -5 %)",   "< 5 ms/sample",
         "Aucune",           "Compatible HE en principe","Degre fixe : gaspille le budget pour noeuds faciles"],
        ["Soft adaptatif (clair)","~= Hard (0 % sur Breast/Heart/Steel)", "< 10 ms/sample",
         "Aucune",           "Optimal en precision avec normalisation locale des logits",
                                                        "Necessite les min_gap (donnees d'entrainement)"],
        ["HE adaptatif (CKKS)","~= Soft adapt. (0 % sur Breast/Heart/Steel)","75 - 385 ms/sample",
         "Complete (IND-CPA)","Inference privee reelle; packing SIMD; coherence clair/HE",
                              "10 - 600x plus lent que le clair"],
    ]
    story.append(make_table(synt_data,
        [2.8*cm, 2.8*cm, 2.8*cm, 2.5*cm, 4.0*cm, 4.1*cm], s))
    story.append(Paragraph("Table 14 - Synthese precision/cout des quatre modes d'inference.", s["caption"]))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §10 LIMITATIONS
    # ══════════════════════════════════════════════
    story.append(h1("10  -  LIMITATIONS ET CAS D'ECHEC OBSERVES", s))
    story.append(sp())

    story.append(h2("10.1  Echecs OOM (code 137 - Out of Memory)", s))
    story.append(body(
        "Le code d'erreur 137 est observe systematiquement pour les datasets a haute "
        "dimensionnalite. Analyse des causes :", s))
    oom_data = [
        ["Dataset",            "Features", "Samples", "Cause",                         "Solution"],
        ["spam (57 features)", "57", "691",
         "slots/sample = 57*8 = 456. Avec N = 16384, packed_samples = 35 "
         "mais les galois keys et moduli chain saturent la memoire Podman.",
         "N = 32768 ou n_max = 4 ou batch plus petit"],
        ["electricity (8f.)",  "8",  "6797",
         "Grand nombre de samples * overhead galois keys",
         "Augmenter memoire Podman ou micro-batches"],
        ["phoneme (5 feat.)",  "5",  "811",
         "Idem -- overhead memoire galois keys",
         "Meme solution"],
        ["spam2 (57 feat.)",   "57", "691",
         "Seuls 2 samples testes avec succes (100% sur 2) -- non representatif",
         "Tests sur batch plus grand avec plus de memoire"],
    ]
    story.append(make_table(oom_data, [2.8*cm, 1.5*cm, 1.5*cm, 6.0*cm, 5.2*cm], s))
    story.append(Paragraph("Table 15 - Analyse des cas d'echec OOM.", s["caption"]))
    story.append(sp(0.5))

    story.append(h2("10.2  Limitations algorithmiques", s))
    for item in [
        "<b>Profondeur exponentielle en clair :</b> SoftTree en clair parcourt 2^D chemins. "
        "Pour D = 10, cela represente 1024 evaluations de phi. La version HE via SumPath "
        "resout ce probleme en O(n_nodes).",
        "<b>Stabilite numerique Vandermonde :</b> Pour les degres eleves (32), la matrice "
        "de Vandermonde est mal conditionnee. L'elimination gaussienne naive peut introduire "
        "des erreurs. Une decomposition QR serait plus stable.",
        "<b>Fenetre morte fixe delta = 0.05 :</b> Pour des datasets ou les features ont des "
        "echelles tres differentes, la normalisation locale des logits est plus efficace "
        "qu'une simple reduction uniforme de delta.",
        "<b>Arbres SortingHat multi-echelle :</b> Les cas Breast, Heart et Steel ont montre "
        "qu'une mauvaise calibration locale des logits, et non CKKS seul, etait la cause "
        "principale de la perte avant correction.",
    ]:
        story.append(bitem(item, s))
    story.append(sp(0.5))
    story.append(warn(
        "Les echecs sur spam (57 features) revelent une limite fondamentale : "
        "le packing HBDT-SumPath avec N = 16 384 slots supporte au maximum "
        "N / (n_feat * n_max) = 16384 / (57 * 8) ~= 35 slots par sample. "
        "Pour des features plus nombreuses, il faut N = 32768 (securite 192-bit) "
        "ou reduire n_max.", s))
    story.append(PageBreak())

    # ══════════════════════════════════════════════
    # §11 CONCLUSION
    # ══════════════════════════════════════════════
    story.append(h1("11  -  CONCLUSION", s))
    story.append(sp())
    story.append(body(
        "Le projet <b>My_POC_All</b> demontre avec succes la faisabilite d'une inference "
        "privee complete sur des arbres de decision reels, en combinant l'algorithme "
        "HBDT-SumPath, l'approximation polynomiale adaptative et le schema CKKS via "
        "Microsoft SEAL.", s))
    story.append(sp(0.5))

    story.append(h2("11.1  Resultats cles", s))
    worst_soft_gap = soft_summary["worst_soft_gap"]
    worst_he_gap = soft_summary["worst_he_gap"]
    he_match_names = ", ".join(display_dataset_name(name) for name in soft_summary["he_matches_clear"])
    he_gain_names = ", ".join(display_dataset_name(name) for name in soft_summary["he_beats_global"])
    for item in [
        "<b>Nouveaux resultats clairs disponibles :</b> Le rapport tient maintenant compte des "
        "trois references du POC principal en clair (hard, soft global, soft adaptatif), ce qui "
        "permet de distinguer la perte due a l'approximation polynomiale de celle introduite par HE.",
        f"<b>Soft adaptatif en clair :</b> Sur les {soft_summary['count']} derniers datasets du POC principal, "
        f"l'ecart moyen au hard est de {fmt_pct(soft_summary['avg_hard_gap'])}. Le cas le plus difficile est "
        f"{display_dataset_name(worst_soft_gap[0])} avec {fmt_delta_pct(-worst_soft_gap[1])} par rapport au hard.",
        f"<b>Coherence clair/HE :</b> L'ecart moyen entre soft adaptatif clair et HE est de "
        f"{fmt_pct(soft_summary['avg_he_gap'])}. Les sorties HE reproduisent exactement le soft adaptatif clair sur "
        f"{he_match_names or '--'}, tandis que l'ecart maximal apparait sur "
        f"{display_dataset_name(worst_he_gap[0])} ({fmt_delta_pct(-worst_he_gap[1])}).",
        f"<b>Comparaison soft global vs. adaptatif en HE :</b> Le soft adaptatif HE depasse le soft global HE sur "
        f"{he_gain_names or 'aucun dataset'} ; ailleurs, les deux variantes restent egales ou legerement a l'avantage "
        "du soft global.",
        "<b>Comparaison inter-projets plus lisible :</b> Le rapport consolide desormais dans un "
        "meme document les runs du POC principal, la baseline Akavia et les variantes SortingHat.",
        "<b>Concrete-ML :</b> Le projet est conserve comme perspective, mais aucun fichier de "
        f"resultats Concrete-ML n'est disponible localement dans ce workspace au {format_date_fr(latest_results_timestamp())}.",
    ]:
        story.append(bitem(item, s))
    story.append(sp(0.5))

    story.append(h2("11.2  Perspectives d'amelioration", s))
    persp_data = [
        ["Amelioration",                  "Impact attendu",                              "Complexite"],
        ["N = 32 768",                    "Support datasets 50+ features; securite 192-bit","Moyenne"],
        ["Delta = 2^50 pour petits gaps", "Reduire degradation HE Cancer/Wine a < 1 %", "Faible"],
        ["Decomposition QR Vandermonde",  "Stabilite numerique polynomes degre 32+",    "Faible"],
        ["Fenetre adaptative par feature","Meilleure precision pour datasets multi-echelle","Moyenne"],
        ["Arbres entraines mini-max loss","Forcer min_gap > delta -> precision = 100 %", "Elevee"],
        ["Foret aleatoire privee",        "Traitement de plusieurs arbres en parallele", "Elevee"],
    ]
    story.append(make_table(persp_data, [5.5*cm, 8.5*cm, 3.0*cm], s))
    story.append(Paragraph("Table 16 - Perspectives d'amelioration.", s["caption"]))
    story.append(sp())
    story.append(note(
        "Ce POC constitue une base solide pour explorer l'inference privee sur des modeles "
        "plus complexes (forets, XGBoost) ou sur des architectures reseaux de neurones (PPNN). "
        "L'integration avec Concrete-ML ou TFHE reste une perspective interessante pour les "
        "faibles profondeurs, mais elle necessitera des mesures experimentales dediees pour "
        "etre comparee proprement au POC actuel.", s))
    story.append(sp(2))
    story.append(hr())
    story.append(Paragraph(
        f"Rapport genere automatiquement  |  My_POC_All  |  Madicke Mbodj  |  {format_date_fr(datetime.now())}",
        s["caption"]))

    return story


def main():
    doc = SimpleDocTemplate(
        OUTPUT_PATH,
        pagesize=A4,
        leftMargin=1.8*cm,
        rightMargin=1.8*cm,
        topMargin=1.8*cm,
        bottomMargin=1.8*cm,
        title="POC - Inference Privee sur Arbres de Decision",
        author="Madicke Mbodj",
        subject="HBDT-SumPath + CKKS + Microsoft SEAL",
    )
    story = build_story()
    doc.build(story, onFirstPage=on_page, onLaterPages=on_page)
    print(f"PDF genere : {OUTPUT_PATH}")

if __name__ == "__main__":
    main()
