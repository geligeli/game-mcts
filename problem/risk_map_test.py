"""risk_map.svg has what risk_replay.js draws on: one outline per territory,
in Country order, with its army count's anchor inside it, and the sea links
between territories that border across water."""

import pathlib
import re
import unittest
import xml.etree.ElementTree as ElementTree

SVG = "{http://www.w3.org/2000/svg}"
MAP = pathlib.Path(__file__).with_name("risk_map.svg")


def Ring(d):
    numbers = [float(n) for n in re.findall(r"-?\d+(?:\.\d+)?", d)]
    return list(zip(numbers[0::2], numbers[1::2]))


def Inside(point, ring):
    x, y = point
    inside = False
    for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]):
        if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
            inside = not inside
    return inside


class RiskMapTest(unittest.TestCase):

    def setUp(self):
        self.root = ElementTree.parse(MAP).getroot()
        self.width, self.height = map(
            float, self.root.get("viewBox").split()[2:])

    def testEveryTerritoryHasAnOutlineAndAnAnchorInsideIt(self):
        paths = self.root.findall(f".//{SVG}path[@class='territory']")
        self.assertEqual([p.get("id") for p in paths],
                         [f"t{t}" for t in range(42)])
        for path in paths:
            anchor = (float(path.get("data-x")), float(path.get("data-y")))
            ring = Ring(path.get("d"))
            self.assertGreaterEqual(len(ring), 3, path.get("data-name"))
            self.assertTrue(Inside(anchor, ring), path.get("data-name"))
            self.assertEqual(path.find(f"{SVG}title").text,
                             path.get("data-name"))

    def testSeaLinksJoinTwoTerritoriesAndOneCrossesTheEdge(self):
        lines = self.root.findall(f".//{SVG}line[@class='sea']")
        self.assertGreater(len(lines), 20)
        for line in lines:
            a, b = int(line.get("data-a")), int(line.get("data-b"))
            self.assertTrue(0 <= a < b < 42)
            for axis, limit in (("x", self.width), ("y", self.height)):
                for end in "12":
                    self.assertTrue(0 <= float(line.get(axis + end)) <= limit)
        wrapped = [l for l in lines if l.get("data-wrap")]
        self.assertEqual(len(wrapped), 2, "Alaska-Kamchatka, off both edges")
        self.assertEqual({l.get("data-a") for l in wrapped}, {"1"})


if __name__ == "__main__":
    unittest.main()
