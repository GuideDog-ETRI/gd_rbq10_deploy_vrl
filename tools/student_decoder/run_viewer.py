"""Verified CPU diagnostic launch. Never starts a simulator or Pilot."""
import argparse
import importlib.util
import json
import os
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--bundle",type=Path,required=True)
    p.add_argument("--binary",type=Path,required=True,help="explicit isolated-build student-decoder-viewer")
    p.add_argument("viewer_args",nargs=argparse.REMAINDER)
    a=p.parse_args()
    args=a.viewer_args
    if args[:1]==["--"]:args=args[1:]
    if "--headless" not in args or "--sim" not in args:
        p.error("this launch helper requires --headless --sim")
    if "--interface" not in args or args[args.index("--interface")+1:args.index("--interface")+2]!=["lo"]:
        p.error("loopback --interface lo required")
    spec=importlib.util.spec_from_file_location("bundle_check",ROOT/"export/check_gast_bundle.py")
    check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)
    bundle=a.bundle.resolve()
    manifest=json.loads((bundle/"manifest.json").read_text())
    for name,digest in manifest["deployment_hashes"].items():
        path=(bundle/name).resolve()
        if path.parent!=bundle or check.sha(path)!=digest:
            p.error("bundle hash/path mismatch: "+name)
    if not check.check_decoder(bundle,manifest):
        p.error("verified decoder missing; export a NEW bundle copy first")
    os.nice(19)
    # Does not set DISPLAY or create a window; explicit binary avoids stale checkout build.
    os.execv(str(a.binary.resolve()),[str(a.binary.resolve()),str(bundle),*args])


if __name__=="__main__":main()
