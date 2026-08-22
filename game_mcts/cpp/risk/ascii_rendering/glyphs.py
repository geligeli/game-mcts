import numpy as np
from PIL import ImageFont, Image, ImageDraw

font = ImageFont.truetype("/large_nfs/risk-game-ai/consola.ttf", 16)
tile_w = int(font.getlength("X"))
ascent, descent = font.getmetrics()
tile_h = ascent + descent

def RenderGlyph(char):
    tile_w = int(font.getlength(char))
    ascent, descent = font.getmetrics()
    tile_h = ascent + descent
    image = Image.new("1", (tile_w, tile_h), (0))
    draw = ImageDraw.Draw(image)
    draw.text((0, 0), char, font=font, fill=(1), font_hinting="mono")
    return np.array(image).astype(np.int32).astype(np.bool)


class GlyphInfo:
    def __init__(self, g: chr, ports: np.array):
        self.g = g
        self.image = RenderGlyph(g)
        self.ports = ports

    def connectable(self, connection_port: np.array):
        return bool(np.any(np.all(self.ports-connection_port == 0, axis=1)))

'''
Port location encoding ascii art:
     
         [0,1]  ┌ [1,1] 
           |   /
         ┌───┐
 [-1,0] -|   | ─ [1,0]
         └───┘
        /  |
[-1,-1] ┘ [0,-1]
'''



GLYPHS = [
    GlyphInfo(' ', ports=np.array([], dtype=np.int32).reshape(0, 2)),
    GlyphInfo('|', ports=np.array([[0,  1], [0, -1]])),
    GlyphInfo('┤', ports=np.array([[0,  1], [0, -1], [-1,  0]])),
    # GlyphInfo('┐', ports=np.array([[-1,  0], [0, -1]])),
    # GlyphInfo('└', ports=np.array([[0, 1], [1, 0]])),
    GlyphInfo('╮', ports=np.array([[-1,  0], [0, -1]])),
    GlyphInfo('╰', ports=np.array([[0, 1], [1, 0]])),
    GlyphInfo('┴', ports=np.array([[-1,  0], [0,  1], [1,  0]])),
    GlyphInfo('┬', ports=np.array([[-1,  0], [0, -1], [1,  0]])),
    GlyphInfo('├', ports=np.array([[0,  1], [1,  0], [0, -1]])),
    GlyphInfo('─', ports=np.array([[-1,  0], [1,  0]])),
    GlyphInfo('┼', ports=np.array([[-1,  0], [0,  1], [1,  0], [0, -1]])),
    # GlyphInfo('┘', ports=np.array([[-1,  0], [0,  1]])),
    GlyphInfo('╯', ports=np.array([[-1,  0], [0,  1]])),
    # GlyphInfo('┌', ports=np.array([[0, -1], [1,  0]])),
    GlyphInfo('╭', ports=np.array([[0, -1], [1,  0]])),
    
    GlyphInfo('_', ports=np.array([[-1, -1], [1,  -1]])),
    GlyphInfo('/', ports=np.array([[-1, -1], [1, 1]])),
    GlyphInfo('\\', ports=np.array([[-1, 1], [1, -1]])),
]

ALL_PORTS = np.array([[-1,  0], [0,  1], [1,  0], [0, -1], [-1, -1], [1,  1], [-1, 1], [1, -1]])

# Build a lookup from frozenset of port tuples -> glyph index.
_PORT_SET_TO_GLYPH_INDEX = {}
for _i, _g in enumerate(GLYPHS):
    _key = frozenset(map(tuple, _g.ports.tolist()))
    _PORT_SET_TO_GLYPH_INDEX[_key] = _i

def glyph_for_char(g: str) -> int:
    """Return the glyph index for the given character."""
    for i, g_ in enumerate(GLYPHS):
        if g_.g == g:
            return i
    raise ValueError(f"No glyph found for character '{g}'")

def glyph_addition(idx_a: int, idx_b: int) -> int:
    """Return the glyph index whose ports are the union of the two input glyphs' ports.

    If no exact match exists, returns the glyph with the largest port-set
    overlap (fewest missing + fewest extra ports).
    """
    ports_a = set(map(tuple, GLYPHS[idx_a].ports.tolist()))
    ports_b = set(map(tuple, GLYPHS[idx_b].ports.tolist()))
    combined = frozenset(ports_a | ports_b)

    # Exact match
    if combined in _PORT_SET_TO_GLYPH_INDEX:
        return _PORT_SET_TO_GLYPH_INDEX[combined]

    # Closest match: minimise symmetric difference in port sets
    best_idx = 0
    best_cost = float('inf')
    for i, g in enumerate(GLYPHS):
        g_ports = frozenset(map(tuple, g.ports.tolist()))
        cost = len(combined ^ g_ports)
        if cost < best_cost:
            best_cost = cost
            best_idx = i
    return best_idx
