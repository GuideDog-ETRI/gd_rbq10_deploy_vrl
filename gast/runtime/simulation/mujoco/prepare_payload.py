"""Generate a vendor-preserving MJCF overlay with an Isaac-style point payload."""
import argparse
import math
from pathlib import Path
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vendor", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mass", type=float, default=6.0)
    args = parser.parse_args()
    if not math.isfinite(args.mass) or args.mass < 0:
        parser.error("payload mass must be finite and nonnegative")
    vendor = args.vendor.resolve()
    source = vendor / "resources/model/rbq/rbq.xml"
    tree = ET.parse(source)
    inertial = tree.find(".//body[@name='base_link']/inertial")
    if inertial is None or "fullinertia" not in inertial.attrib:
        raise ValueError("Expected base_link with explicit fullinertia")
    if any(key in inertial.attrib for key in ("quat", "euler", "axisangle", "xyaxes", "zaxis")):
        raise ValueError("Expected body-aligned base inertia")
    mass = float(inertial.attrib["mass"])
    com = list(map(float, inertial.attrib["pos"].split()))
    inertia = list(map(float, inertial.attrib["fullinertia"].split()))
    payload_pos = (0.0, 0.0, 0.07)
    displacement = [p - c for p, c in zip(payload_pos, com)]
    total = mass + args.mass
    reduced = mass * args.mass / total
    squared = sum(d * d for d in displacement)
    pairs = ((0, 0), (1, 1), (2, 2), (0, 1), (0, 2), (1, 2))
    combined = [value + reduced * ((squared if i == j else 0) - displacement[i] * displacement[j])
                for value, (i, j) in zip(inertia, pairs)]
    new_com = [c + args.mass / total * d for c, d in zip(com, displacement)]
    inertial.set("mass", format(total, ".12g"))
    inertial.set("pos", " ".join(format(c, ".12g") for c in new_com))
    inertial.set("fullinertia", " ".join(format(v, ".12g") for v in combined))
    # Absolute container paths avoid changing mesh/texture resolution on relocation.
    for element in tree.iter():
        if "file" in element.attrib:
            # MuJoCo resolves assets relative to the top-level rbq_environment.xml.
            asset = (vendor / "resources/model" / element.attrib["file"]).resolve()
            if not asset.is_file():
                raise FileNotFoundError(asset)
            relative = asset.relative_to(vendor)
            element.set("file", str(Path("/workspace/RBQ") / relative))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    tree.write(args.output, encoding="utf-8", xml_declaration=True)
    print(f"[payload] +{args.mass:g} kg at {payload_pos}; base mass={total:g} kg; COM={new_com}")


if __name__ == "__main__":
    main()
