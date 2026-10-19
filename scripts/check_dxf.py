#!/usr/bin/env python3
"""Independent DXF fixture audit. ezdxf is a DEVELOPMENT-ONLY optional dependency."""
import math
import pathlib
import sys

import ezdxf


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    root = pathlib.Path(sys.argv[1])
    for name in ("rectangle", "circle", "washer"):
        doc = ezdxf.readfile(root / (name + ".dxf"))
        require(doc.dxfversion == "AC1015", "DXF version")
        require(doc.units == 4, "Millimetre units")
        audit = doc.audit()
        require(not audit.has_errors and not audit.has_fixes, f"DXF audit: {name}: {audit.errors}, {audit.fixes}")
        entities = list(doc.modelspace())
        require(all(e.dxf.layer == "CUT" for e in entities), "Cutting layer")
        if name == "rectangle":
            require(len(entities) == 1 and entities[0].dxftype() == "LWPOLYLINE", "Rectangle topology")
            line = entities[0]
            require(line.closed, "Closed rectangle")
            require(list(line.get_points("xy")) == [(0, 0), (120.5, 0), (120.5, 80.25), (0, 80.25)], "Rectangle vertices")
        else:
            require(all(e.dxftype() == "CIRCLE" for e in entities), "True circles")
            expected = [40.25] if name == "circle" else [25.125, 50.25]
            require(len(entities) == len(expected), "Contour count")
            require(all(math.isclose(a, b) for a, b in zip(sorted(e.dxf.radius for e in entities), expected)), "Circle radii")
            if name == "washer":
                require(entities[0].dxf.center == entities[1].dxf.center, "Concentric washer")
        print(f"PASS: {name} ({len(entities)} contours), ezdxf read + audit")


if __name__ == "__main__":
    main()
