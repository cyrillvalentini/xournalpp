#!/usr/bin/env python3
"""
Generate GTK "-symbolic" icons from the stroke-based Lucide icon set.

GTK (3.24.x and 4) recolours symbolic icons by injecting a stylesheet:

    rect,circle,path { fill: <fg> !important; stroke: none !important }
    rect.stroke,circle.stroke,path.stroke { fill: none !important; stroke: <fg> !important }
    .error / .warning / .success (and their .stroke variants) -> theme status colours

GTK 4.20 and newer render symbolic icons with their own SVG renderer instead, which ignores those classes
and takes the colours from GTK's own attributes (https://www.gtk.org/grappa namespace):

    gpa:fill="foreground" / gpa:stroke="foreground"   (or "error", "success", "warning", "accent")

Every shape gets both, so the icons work with GTK 3 and all GTK 4 versions.

So every shape only needs the right class:
  * stroked shape          -> class="stroke"
  * filled shape           -> no class
  * filled *and* stroked   -> two copies, one of each
  * drawn in red / green   -> additionally "error" / "success"

`<use>` elements are inlined, and the colours from the original file are dropped.

Usage: make_symbolic_icons.py <lucide-actions-dir> <output-theme-dir>
"""

import copy
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SVG = "http://www.w3.org/2000/svg"
XLINK = "http://www.w3.org/1999/xlink"
GPA = "https://www.gtk.org/grappa"
ET.register_namespace("", SVG)
ET.register_namespace("gpa", GPA)

SHAPES = {"path", "circle", "rect"}  # the elements GTK's injected stylesheet targets
ERROR_COLOURS = {"#ed1b24"}
SUCCESS_COLOURS = {"#00a129"}
INHERITED = ("fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin", "stroke-miterlimit",
             "stroke-dasharray", "stroke-dashoffset", "fill-rule", "opacity", "fill-opacity", "stroke-opacity")

INDEX_THEME = """[Icon Theme]
Name=Hicolor
Comment=Xournal++ symbolic icons (generated from the Lucide set by scripts/make_symbolic_icons.py)
Hidden=true
Directories=scalable/actions

[scalable/actions]
MinSize=1
Size=16
MaxSize=256
Context=Actions
Type=Scalable
"""


def local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def parse_style(s):
    out = {}
    for part in (s or "").split(";"):
        if ":" in part:
            k, v = part.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def props_of(el, inherited: dict) -> dict:
    here = dict(inherited)
    for p in INHERITED:
        if el.get(p) is not None:
            here[p] = el.get(p)
    for k, v in parse_style(el.get("style")).items():
        if k in INHERITED:
            here[k] = v
    return here


def visible(p) -> bool:
    return p is not None and p.lower() not in ("none", "transparent")


def status_class(colour):
    c = (colour or "").lower()
    if c in ERROR_COLOURS:
        return "error"
    if c in SUCCESS_COLOURS:
        return "success"
    return None


def emit(el, props: dict, out_parent, by_id: dict):
    tag = local(el.tag)
    if tag in ("defs", "metadata", "title", "desc", "linearGradient", "radialGradient", "style"):
        return
    if not el.tag.startswith("{" + SVG + "}"):
        return  # sodipodi / inkscape editor elements
    here = props_of(el, props)

    if tag == "use":
        ref = (el.get(f"{{{XLINK}}}href") or el.get("href") or "").lstrip("#")
        if ref in by_id:
            g = ET.SubElement(out_parent, f"{{{SVG}}}g")
            tx, ty = el.get("x", "0"), el.get("y", "0")
            transform = " ".join(t for t in (el.get("transform"), f"translate({tx},{ty})") if t)
            g.set("transform", transform)
            emit(by_id[ref], here, g, by_id)
        return

    if tag == "g" or tag == "svg":
        g = ET.SubElement(out_parent, f"{{{SVG}}}g")
        for a in ("transform", "clip-path"):
            if el.get(a):
                g.set(a, el.get(a))
        if "opacity" in parse_style(el.get("style")) or el.get("opacity"):
            g.set("opacity", here["opacity"])
        for c in el:
            emit(c, here, g, by_id)
        return

    if tag not in SHAPES:
        print(f"  warning: unsupported <{tag}> skipped", file=sys.stderr)
        return

    geometry = {k: v for k, v in el.attrib.items()
                if k not in INHERITED and k not in ("style", "class", "id") and not k.startswith("{")}
    fill, stroke = here.get("fill", "black"), here.get("stroke", "none")

    def add(kind: str, colour):
        e = ET.SubElement(out_parent, f"{{{SVG}}}{tag}", geometry)
        status = status_class(colour)
        classes = [c for c in (kind, status) if c]
        if classes:
            e.set("class", " ".join(classes))
        # GTK >= 4.20: explicit paint plus the symbolic colour to substitute
        if kind == "stroke":
            e.set("fill", "none")
            e.set("stroke", "rgb(0,0,0)")
            e.set(f"{{{GPA}}}stroke", status or "foreground")
        else:
            e.set("fill", "rgb(0,0,0)")
            e.set(f"{{{GPA}}}fill", status or "foreground")
        if kind == "stroke":
            for p in INHERITED[2:]:
                if p in here and p.startswith("stroke") and p != "stroke-opacity":
                    e.set(p, here[p])
        if here.get("fill-rule") and kind != "stroke":
            e.set("fill-rule", here["fill-rule"])

    if visible(fill):
        add("", fill)
    if visible(stroke):
        add("stroke", stroke)


def convert(src: Path) -> str:
    root = ET.parse(src).getroot()
    by_id = {e.get("id"): e for e in root.iter() if e.get("id")}
    out = ET.Element(f"{{{SVG}}}svg", {
        "width": "16", "height": "16", f"{{{GPA}}}version": "1",
        "viewBox": root.get("viewBox") or f"0 0 {root.get('width', '24')} {root.get('height', '24')}"})
    # keep clip paths (geometry only) so clip-path references still resolve
    clips = [e for e in root.iter() if local(e.tag) == "clipPath"]
    if clips:
        defs = ET.SubElement(out, f"{{{SVG}}}defs")
        for c in clips:
            defs.append(copy.deepcopy(c))
    base = props_of(root, {"fill": "black", "stroke": "none"})
    for c in root:
        emit(c, base, out, by_id)
    return ET.tostring(out, encoding="unicode")


def main():
    src_dir, out_theme = Path(sys.argv[1]), Path(sys.argv[2])
    out_dir = out_theme / "hicolor" / "scalable" / "actions"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_theme / "hicolor" / "index.theme").write_text(INDEX_THEME)
    n = 0
    for svg in sorted(src_dir.glob("*.svg")):
        print(svg.name, file=sys.stderr) if "-v" in sys.argv else None
        (out_dir / f"{svg.stem}-symbolic.svg").write_text(convert(svg) + "\n")
        n += 1
    print(f"wrote {n} icons to {out_dir}")


if __name__ == "__main__":
    main()
