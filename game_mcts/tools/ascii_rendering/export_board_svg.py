"""Writes problem/risk_map.svg, the map the browser replay draws on.

    python3 game_mcts/tools/ascii_rendering/export_board_svg.py \
        /fast/src/risk-game-ai/notebooks/board_segmentation.json \
        -o problem/risk_map.svg

The territory outlines are risk-game-ai's COCO annotation of a photo of the
board, mapped onto the board's own 30x20 inch plane exactly as
risk_board_polygons.LoadBoardPolygons() does (there with OpenCV, here with
the same 4-point perspective transform solved by hand, so that this needs
nothing but the standard library), then scaled to a 900x600 viewBox and
simplified. The i-th territory annotation (the "Board" outline skipped) is
template territory id i+1, which kCountryToTerritoryId in ascii_board.h maps
the Country enum onto; names and neighbours come from risk_board.h.

The SVG has one <path id="t{country}"> per territory, with its name, an
anchor for its army count (data-x, data-y: the area centroid), and a dashed
<line class="sea"> for each border the outlines do not touch -- the sea
links. Alaska and Kamchatka border across the map's edge, so theirs is a stub
off each edge (data-wrap).
"""

import argparse
import json
import math
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
RISK_BOARD_H = REPO / "game_mcts/games/risk/risk_board.h"
ASCII_BOARD_H = REPO / "game_mcts/games/risk/ascii/ascii_board.h"

BOARD_W, BOARD_H = 30 * 300, 20 * 300  # risk_board_polygons: 300 DPI
VIEW_W, VIEW_H = 900, 600
SCALE = VIEW_W / BOARD_W
SIMPLIFY = 1.0  # viewBox units, i.e. 10 board units
TOUCH = 1.0  # outlines closer than this share a border


def ParseBoard():
    """Country names in enum order, and each one's neighbours."""
    text = RISK_BOARD_H.read_text()
    enum = re.search(r"enum class Country[^{]*\{(.*?)\}", text, re.S).group(1)
    countries = [c.strip() for c in enum.replace("\n", " ").split(",")]
    countries = [c for c in countries if c and c != "COUNT"]
    names, neighbours = {}, {}
    for match in re.finditer(
            r'\{Country::(\w+),\s*"([^"]+)",\s*(\d+),\s*\{([^}]*)\}\}', text):
        country, name, _, adjacent = match.groups()
        names[country] = name
        neighbours[country] = re.findall(r"Country::(\w+)", adjacent)
    assert len(countries) == 42 and len(names) == 42, "risk_board.h changed"
    return countries, names, neighbours


def ParseTerritoryIds():
    text = ASCII_BOARD_H.read_text()
    body = re.search(r"kCountryToTerritoryId = \{(.*?)\};", text, re.S).group(1)
    ids = [int(n) for n in re.findall(r"\b\d+\b", re.sub(r"//[^\n]*", "", body))]
    assert len(ids) == 42, "ascii_board.h changed"
    return ids


def Solve(a, b):
    """x with a x = b, by Gaussian elimination with partial pivoting."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(m[r][col]))
        m[col], m[pivot] = m[pivot], m[col]
        for r in range(n):
            if r != col:
                f = m[r][col] / m[col][col]
                m[r] = [x - f * y for x, y in zip(m[r], m[col])]
    return [m[i][n] / m[i][i] for i in range(n)]


def PerspectiveTransform(src, dst):
    """The homography taking the 4 points |src| to |dst|, as a function."""
    a, b = [], []
    for (x, y), (u, v) in zip(src, dst):
        a.append([x, y, 1, 0, 0, 0, -u * x, -u * y])
        b.append(u)
        a.append([0, 0, 0, x, y, 1, -v * x, -v * y])
        b.append(v)
    h = Solve(a, b) + [1.0]

    def apply(x, y):
        w = h[6] * x + h[7] * y + h[8]
        return ((h[0] * x + h[1] * y + h[2]) / w,
                (h[3] * x + h[4] * y + h[5]) / w)

    return apply


def Simplify(points, epsilon):
    """Douglas-Peucker on an open polyline."""
    if len(points) < 3:
        return points
    (x0, y0), (x1, y1) = points[0], points[-1]
    dx, dy = x1 - x0, y1 - y0
    norm = math.hypot(dx, dy) or 1.0
    far, index = 0.0, 0
    for i, (x, y) in enumerate(points[1:-1], 1):
        d = abs(dy * (x - x0) - dx * (y - y0)) / norm
        if d > far:
            far, index = d, i
    if far <= epsilon:
        return [points[0], points[-1]]
    return (Simplify(points[:index + 1], epsilon)[:-1] +
            Simplify(points[index:], epsilon))


def SimplifyRing(ring, epsilon):
    # Split at the vertex farthest from the first, so both halves are open.
    far = max(range(len(ring)), key=lambda i: math.dist(ring[0], ring[i]))
    first = Simplify(ring[:far + 1], epsilon)
    second = Simplify(ring[far:] + [ring[0]], epsilon)
    return first[:-1] + second[:-1]


def Centroid(ring):
    area = cx = cy = 0.0
    for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]):
        cross = x0 * y1 - x1 * y0
        area += cross
        cx += (x0 + x1) * cross
        cy += (y0 + y1) * cross
    return cx / (3 * area), cy / (3 * area)


def SegmentDistance(p, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = 0.0 if dx == dy == 0 else max(
        0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / (dx * dx + dy * dy)))
    return math.dist(p, (ax + t * dx, ay + t * dy)), (ax + t * dx, ay + t * dy)


def Closest(ring_a, ring_b):
    """The closest points of two outlines, and their distance."""
    best = (math.inf, None, None)
    for ring, other, swap in ((ring_a, ring_b, False), (ring_b, ring_a, True)):
        for p in ring:
            for a, b in zip(other, other[1:] + other[:1]):
                d, q = SegmentDistance(p, a, b)
                if d < best[0]:
                    best = (d, q, p) if swap else (d, p, q)
    return best


def Number(value):
    text = f"{value:.1f}"
    return text[:-2] if text.endswith(".0") else text


def PathData(ring):
    head, *rest = ring
    return ("M" + Number(head[0]) + " " + Number(head[1]) + "L" +
            " ".join(Number(x) + " " + Number(y) for x, y in rest) + "Z")


def Main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("coco", help="risk-game-ai's board_segmentation.json")
    parser.add_argument("-o", "--output", default="problem/risk_map.svg")
    args = parser.parse_args()

    countries, names, neighbours = ParseBoard()
    territory_ids = ParseTerritoryIds()

    coco = json.loads(pathlib.Path(args.coco).read_text())
    category = {c["id"]: c["name"] for c in coco["categories"]}
    by_name = {}  # as LoadBoardPolygons: file order, the last of a name wins
    for annotation in coco["annotations"]:
        by_name[category[annotation["category_id"]]] = annotation

    def Points(annotation):
        flat = annotation["segmentation"][0]
        return list(zip(flat[0::2], flat[1::2]))

    to_board = PerspectiveTransform(
        Points(by_name["Board"]),
        [(0, 0), (0, BOARD_H), (BOARD_W, BOARD_H), (BOARD_W, 0)])
    outlines = []  # by template territory id - 1
    for name, annotation in by_name.items():
        if name == "Board":
            continue
        ring = [to_board(x, y) for x, y in Points(annotation)]
        outlines.append([(x * SCALE, y * SCALE) for x, y in ring])
    assert len(outlines) == 42, "board_segmentation.json changed"

    rings = [SimplifyRing(outlines[territory_ids[c] - 1], SIMPLIFY)
             for c in range(42)]
    index = {name: i for i, name in enumerate(countries)}

    sea = []
    for a, country in enumerate(countries):
        for other in neighbours[country]:
            b = index[other]
            if b < a:
                continue
            gap, pa, pb = Closest(rings[a], rings[b])
            if gap <= TOUCH:
                continue
            if abs(pa[0] - pb[0]) > VIEW_W / 2:  # across the map's edge
                left, right = (pa, pb) if pa[0] < pb[0] else (pb, pa)
                sea.append((a, b, left, (0, left[1]), True))
                sea.append((a, b, right, (VIEW_W, right[1]), True))
            else:
                sea.append((a, b, pa, pb, False))

    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d">' %
        (VIEW_W, VIEW_H),
        "<!-- Generated by game_mcts/tools/ascii_rendering/export_board_svg.py "
        "from risk-game-ai's board_segmentation.json; do not edit. -->",
        '<g id="sea">',
    ]
    for a, b, p, q, wrap in sea:
        lines.append(
            '<line class="sea" data-a="%d" data-b="%d"%s x1="%s" y1="%s" '
            'x2="%s" y2="%s"/>' %
            (a, b, ' data-wrap="1"' if wrap else "", Number(p[0]),
             Number(p[1]), Number(q[0]), Number(q[1])))
    lines.append('</g>\n<g id="territories">')
    for c, ring in enumerate(rings):
        x, y = Centroid(ring)
        name = names[countries[c]]
        lines.append(
            '<path id="t%d" class="territory" data-name="%s" data-x="%s" '
            'data-y="%s" d="%s"><title>%s</title></path>' %
            (c, name, Number(x), Number(y), PathData(ring), name))
    lines.append("</g>\n</svg>\n")
    pathlib.Path(args.output).write_text("\n".join(lines))
    print("%s: %d territories, %d points, %d sea links, %d bytes" %
          (args.output, len(rings), sum(len(r) for r in rings), len(sea),
           len("\n".join(lines))))


if __name__ == "__main__":
    Main()
