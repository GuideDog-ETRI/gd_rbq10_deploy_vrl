"""Hash-check a GAST bundle using only the Python standard library."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_decoder(bundle, manifest):
    contract = manifest.get("student_decoder_contract")
    if contract is None:
        return False  # legacy bundles explicitly report no verified decoder
    if contract != "gast.student_decoder.v1":
        raise ValueError("unsupported decoder contract")
    meta = json.loads((bundle / "student_decoder.json").read_text())
    hashes = manifest["deployment_hashes"]
    if not all(name in hashes for name in ("student_decoder.json", "student_decoder.onnx")):
        raise ValueError("decoder files must be bound by manifest hashes")
    for name in ("student_decoder.json", "student_decoder.onnx"):
        if sha(bundle/name) != hashes[name]:
            raise ValueError("decoder manifest hash mismatch")
    if meta.get("source_sha256") != manifest["student_checkpoint_sha256"]:
        raise ValueError("decoder belongs to a different student")
    for key in ("student_iteration", "teacher_sha256", "camera_contract"):
        if meta.get(key) != manifest.get(key):
            raise ValueError("decoder metadata mismatch: "+key)
    if (meta.get("grid",{}).get("rows")!=11 or meta.get("grid",{}).get("cols")!=17
            or meta.get("channels")!=["height_m","teacher_visible","gap","step_up","step_down","support"]):
        raise ValueError("decoder grid/channel mismatch")
    if meta.get("onnx_sha256") != sha(bundle / "student_decoder.onnx"):
        raise ValueError("decoder ONNX hash mismatch")
    error=meta.get("parity_max_abs_error")
    if type(error) not in (int,float) or not math.isfinite(error) or not 0 <= error < 1e-4:
        raise ValueError("decoder parity missing/failed")
    if (meta.get("input_shape") != [1,6116] or meta.get("output_shape") != [1,187,6]
            or meta.get("schema") != contract or meta.get("student_arch") != "gast_spatiotemporal_v1"):
        raise ValueError("decoder structure mismatch")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("resource", help="resource directory relative to resources/policy")
    parser.add_argument("--expected-iteration", type=int, required=True)
    parser.add_argument("--require-decoder", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    bundle = root / "resources/policy" / args.resource
    manifest_path = bundle / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("status") != "offline_parity_passed_simulation_ready_NOT_smoke_tested":
        raise SystemExit(f"Bundle is not marked offline-parity-passed: {manifest.get('status')}")
    for name, expected in manifest["deployment_hashes"].items():
        path = bundle / name
        if not path.is_file():
            raise SystemExit(f"Missing bundle file: {path}")
        actual = sha(path)
        if actual != expected:
            raise SystemExit(f"SHA256 mismatch: {path} expected={expected} actual={actual}")
    if manifest.get("student_iteration") != args.expected_iteration:
        raise SystemExit(f"Unexpected student iteration: {manifest.get('student_iteration')} expected {args.expected_iteration}")
    verified_decoder = check_decoder(bundle, manifest)
    if args.require_decoder and not verified_decoder:
        raise SystemExit("legacy bundle has no verified decoder; export a NEW copy")
    print("decoder: verified" if verified_decoder else "decoder: unavailable (legacy bundle)")
    print(f"GAST bundle OK: {bundle}")
    print(f"student iteration={manifest['student_iteration']} teacher SHA256={manifest['teacher_sha256']}")
    print("Offline parity passed; MuJoCo smoke test has NOT been run.")


if __name__ == "__main__":
    main()
