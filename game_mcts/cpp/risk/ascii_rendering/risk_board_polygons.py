import json
import numpy as np
import cv2 as cv
import numpy as np

DPI = 300
HEIGHT = DPI*20
WIDTH = DPI*30

def LoadBoardPolygons() -> list[np.ndarray]:
    coco_segmentation = json.load(
        open('/large_nfs/risk-game-ai/notebooks/board_segmentation.json'))
    id_to_name = {x['id']: x['name'] for x in coco_segmentation['categories']}
    segmentations_by_name = {}

    for segmentation in coco_segmentation['annotations']:
        name = id_to_name[segmentation['category_id']]
        segmentations_by_name[name] = segmentation

    def segmentationToXy(segmentation):
        return np.array(segmentation['segmentation'][0], dtype=np.float32).reshape(-1, 2)

    board_poly = segmentationToXy(segmentations_by_name['Board'])

    target = np.array([[0, 0], [0, 20], [30, 20], [30, 0]], dtype=np.float32)*DPI
    image_to_board = cv.getPerspectiveTransform(board_poly, target)
    all_polys = []
    for name, segmentation in segmentations_by_name.items():
        if name == 'Board':
            continue
        poly = segmentationToXy(segmentation)
        poly = cv.perspectiveTransform(poly[None, :, :], image_to_board)[0]
        poly = np.vstack([poly, poly[0]])
        all_polys.append(poly)
    return all_polys

def polyToMask(all_polys: list[np.ndarray], desired_width : int) -> np.ndarray:
    scale_factor = desired_width / WIDTH
    img = np.zeros((int(HEIGHT*scale_factor),
                   int(WIDTH*scale_factor)), dtype=np.uint8)
    for p in all_polys:
        img = cv.polylines(
            img, [(p*scale_factor).astype(np.int32)], True, 255, thickness=1)
    return img.astype(bool)

