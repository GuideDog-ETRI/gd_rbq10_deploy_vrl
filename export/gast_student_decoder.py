"""Export the GAST student's terrain decoder: hidden_out -> what its memory says the ground looks like.

The GAST student keeps one 32-d memory per terrain cell (187 = 11 x 17, first 5984 values of hidden_out).
During distillation ``spatial_head`` (Linear 32 -> 6) decodes each cell into the auxiliary targets of
gd_lab.gast.geometry.targets; this file exports that head alone so a viewer can show, next to the camera
input, the terrain the student's hidden state encodes. The policy ONNX (student_gast.onnx) is not changed.

  python3 export/gast_student_decoder.py --bundle gast/<bundle name> [--checkpoint <student .pt>]
      (default checkpoint: the bundle's own student_checkpoint.pt)
      -> resources/policy/gast/<bundle>/student_decoder.onnx + student_decoder.json

Output decoded[1,187,6], cell = row*17 + col, row = lateral y from -0.5 m (right) to +0.5 m (left),
col = forward x from -0.8 m to +0.8 m, yaw-aligned body frame:
  0 height_m      scanner_z - ground_z - 0.5 (m), regression
  1 teacher_visible  probability the teacher would see the cell
  2 gap              probability of a gap
  3 step_up          probability of an upward edge to the next forward cell
  4 step_down        probability of a downward edge
  5 support          probability of a flat foothold
Channels 1..5 are sigmoid of the training logits.
"""
import argparse
import json
import os
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch
from torch import nn

ROOT = Path(__file__).resolve().parents[1]
TRAIN = Path(os.environ.get("GD_LAB_TRAIN_ROOT", ROOT.parent / "gd_lab_vrl")).resolve()
for path in (TRAIN / "gast/src", TRAIN / "src"):  # main layout first, merged layout second
    if (path / "gd_lab/gast/student.py").is_file():
        sys.path.insert(0, str(path))
        break
from gd_lab.gast.student import GastStudent  # noqa: E402

CELLS, CELL_DIM, CHANNELS = 187, 32, 6
CHANNEL_NAMES = ["height_m", "teacher_visible", "gap", "step_up", "step_down", "support"]


class Decoder(nn.Module):
    def __init__(self, spatial_head):
        super().__init__()
        self.head = spatial_head

    def forward(self, hidden):
        memory = hidden[:, :CELLS * CELL_DIM].reshape(-1, CELLS, CELL_DIM)
        out = self.head(memory)
        return torch.cat((out[..., :1], torch.sigmoid(out[..., 1:])), -1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bundle", required=True, help="bundle under resources/policy, e.g. gast/gastv21_2000_student17200_top1_20261008")
    ap.add_argument("--checkpoint", help="GAST student checkpoint; default <bundle>/student_checkpoint.pt")
    args = ap.parse_args()
    args.checkpoint = args.checkpoint or str(ROOT / "resources/policy" / args.bundle / "student_checkpoint.pt")
    ckpt = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
    if ckpt.get("student_arch") != "gast_spatiotemporal_v1":
        raise SystemExit(f"not a GAST student checkpoint: {ckpt.get('student_arch')}")
    student = GastStudent(**ckpt.get("student_config", {})).eval()
    student.load_state_dict(ckpt["model"], strict=True)
    decoder = Decoder(student.spatial_head).eval()
    out_dir = ROOT / "resources/policy" / args.bundle
    if not out_dir.is_dir():
        raise SystemExit(f"bundle not found: {out_dir}")
    onnx_path = out_dir / "student_decoder.onnx"
    torch.onnx.export(decoder, (torch.zeros(1, student.gru_hidden_dim),), str(onnx_path), opset_version=17,
                      dynamo=False, input_names=["hidden"], output_names=["decoded"])

    # Parity: the decoder applied to hidden_out must equal the spatial output of the same student step.
    session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    hidden, worst = torch.zeros(1, student.gru_hidden_dim), 0.0
    with torch.no_grad():
        for step in range(6):
            yaw = .1 * step
            pose = torch.tensor([[.02 * step, 0., yaw, float(np.cos(yaw / 2)), 0., 0., float(np.sin(yaw / 2))]])
            _, hidden, spatial, _, _ = student.encode(torch.rand(1, 4, 2, 45, 80), hidden, pose, 0.0, torch.ones(1))
            expected = torch.cat((spatial[..., :1], torch.sigmoid(spatial[..., 1:])), -1).numpy()
            actual = session.run(None, {"hidden": hidden.numpy()})[0]
            worst = max(worst, float(np.abs(actual - expected).max()))
    if worst >= 1e-4:
        raise SystemExit(f"decoder parity failed: {worst}")
    meta = {"onnx": onnx_path.name, "input": "hidden_out of student_gast.onnx [1,6116]", "output": "decoded[1,187,6]",
            "channels": CHANNEL_NAMES, "grid": {"rows": 11, "cols": 17, "row_axis": "y lateral, -0.5..+0.5 m",
                                                "col_axis": "x forward, -0.8..+0.8 m", "frame": "yaw-aligned body"},
            "height_note": "scanner_z - ground_z - 0.5 m; flat ground under the robot reads about 0",
            "source_checkpoint": str(Path(args.checkpoint).resolve()), "student_iteration": ckpt.get("iteration"),
            "teacher_sha256": ckpt.get("teacher_sha256"), "parity_max_abs_error": worst}
    (out_dir / "student_decoder.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({"output": str(onnx_path), "parity_max_abs_error": worst}))


if __name__ == "__main__":
    main()
