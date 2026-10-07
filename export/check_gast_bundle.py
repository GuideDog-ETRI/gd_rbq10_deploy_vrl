"""Hash-check a GAST bundle using only the Python standard library."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("resource", help="resource directory relative to resources/policy")
    parser.add_argument("--expected-iteration", type=int, required=True)
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
    print(f"GAST bundle OK: {bundle}")
    print(f"student iteration={manifest['student_iteration']} teacher SHA256={manifest['teacher_sha256']}")
    print("Offline parity passed; MuJoCo smoke test has NOT been run.")


if __name__ == "__main__":
    main()
