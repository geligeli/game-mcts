import numpy as np
import cv2


def ClosestPointsDistnace(a, b):
    a = a.astype(np.float64)
    b = b.astype(np.float64)
    a_squard = np.sum(a*a, axis=1)
    b_squard = np.sum(b*b, axis=1)
    ab = np.dot(a, b.T)
    distanceSquared = a_squard[:, None] + b_squard - 2*ab
    return np.sqrt(np.max([distanceSquared.min(), 0.0]))


def PolygonGraph(all_polys: list) -> dict[int, set[int]]:
    adjecencyMatrix = {}
    for i in range(len(all_polys)):
        adjecencyMatrix[i] = set()
    for i in range(len(all_polys)):
        for j in range(i+1, len(all_polys)):
            distance = ClosestPointsDistnace(all_polys[i], all_polys[j])
            if distance > 500.0:
                continue
            adjecencyMatrix[i].add(j)
            adjecencyMatrix[j].add(i)
    return adjecencyMatrix


def findFirstFreeColor(sorted_list: list[int]) -> int:
    for i, c in enumerate(sorted_list):
        if i == c:
            continue
        return i
    return len(sorted_list)


def ColorGraph(adjecencyMatrix: dict[int, set[int]]) -> dict[int, int]:
    colors = {}
    for i in adjecencyMatrix:
        neighbourhoodColors = set()
        for n in adjecencyMatrix[i]:
            if n in colors:
                neighbourhoodColors.add(colors[n])
        colors[i] = findFirstFreeColor(sorted(neighbourhoodColors))
    return colors

DPI = 300
HEIGHT = DPI*20
WIDTH = DPI*30

COLORS = [
    (255, 0, 0),
    (0, 255, 0),
    (0, 0, 255),
    (255, 255, 0),
    (255, 0, 255),
    (0, 255, 255),
    (128, 128, 0),
    (128, 0, 128),
    (0, 128, 128),
]

def polysToImage(all_polys, desired_width):
    g = PolygonGraph(all_polys)
    colors = ColorGraph(g)
    scale_factor = desired_width / WIDTH
    img = np.zeros((int(HEIGHT*scale_factor),
                   int(WIDTH*scale_factor), 3), dtype=np.uint8)
    for i, p in enumerate(all_polys):
        color = COLORS[colors[i] % len(COLORS)]
        img = cv2.fillPoly(
            img, [(p*scale_factor).astype(np.int32)], color, lineType=cv2.LINE_AA)
    return img
