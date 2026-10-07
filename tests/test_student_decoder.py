"""No GPU, DDS or simulator. Run with CPU numpy installed."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np
import os
import sys
import subprocess
import copy
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"export"))
spec=importlib.util.spec_from_file_location("decoder_offline",ROOT/"tools/student_decoder/offline.py")
offline=importlib.util.module_from_spec(spec);spec.loader.exec_module(offline)

class DecoderTests(unittest.TestCase):
    def test_grid_direction(self):
        self.assertEqual(offline.display_cell(0,0),(16,10))
        self.assertEqual(offline.display_cell(10,16),(0,0))
        self.assertEqual(offline.display_cell(5,8),(8,5))
        with self.assertRaises(ValueError):offline.display_cell(11,0)
        record={"sequence":1,"input_stamp_ms":100,"observer_ms":110,
                "frames":np.zeros((4,2,45,80)),"decoded":np.zeros((187,6)),"latent":np.zeros(32)}
        with tempfile.TemporaryDirectory() as d:
            out=Path(d,"render.html")
            offline.render([record],out,height_min=-.4,height_max=.6,dim_visibility=True)
            # Host Node executes this actual generated page after container tests.
            html_out=os.environ.get("DECODER_HTML_TEST_OUT")
            self.assertTrue(html_out,"use tests/run_student_decoder_tests.sh for host/container bridge")
            offline.render([record],html_out,height_min=-.4,height_max=.6,dim_visibility=True)

    def test_cpp_record_roundtrip(self):
        d=os.environ.get("DECODER_CPP_RECORD_DIR")
        self.assertTrue(d,"run host C++ fixture with tests/run_student_decoder_tests.sh")
        if d:
            frame=offline.read_frame(Path(d,"frame_test.json"))
            self.assertEqual(frame["sequence"],1234567890123)
            self.assertEqual(frame["input_stamp_ms"],1234567890000)
            for key,value in (("frames",.25),("hidden",.123),("latent",-.5),("decoded",.75)):
                np.testing.assert_allclose(frame[key],value,atol=1e-7)
            self.assertEqual(frame["capture_pose_status"],"unavailable")
            self.assertFalse(Path(d,"too_big.json").exists())
            self.assertFalse(Path(d,"invalid.json").exists())

    def test_random_student_onnx_parity(self):
        os.environ.setdefault("GD_LAB_TRAIN_ROOT",str(Path.home()/"gd_project/gd_lab_vrl"))
        import torch
        from gast_student_decoder import GastStudent,export_decoder
        from check_gast_bundle import check_decoder,sha
        torch.set_num_threads(1);torch.manual_seed(123)
        student=GastStudent().eval()
        with tempfile.TemporaryDirectory() as d:
            base=Path(d);ckpt=base/"random.pt";out=base/"decoder"
            torch.save({"student_arch":student.architecture,"model":student.state_dict(),
                        "student_config":{},"iteration":42,"teacher_sha256":"synthetic",
                        "camera_contract":{"profile":"synthetic-test"}},ckpt)
            meta=export_decoder(ckpt,out)
            self.assertLess(meta["parity_max_abs_error"],1e-4)
            manifest={"student_decoder_contract":meta["schema"],"student_checkpoint_sha256":sha(ckpt),
                      "student_iteration":42,"teacher_sha256":"synthetic","camera_contract":meta["camera_contract"],
                      "deployment_hashes":{p.name:sha(p) for p in out.iterdir()}}
            self.assertTrue(check_decoder(out,manifest))
            self.assertFalse(check_decoder(out,{}))  # legacy accepted without claiming a decoder
            for key,value in (("teacher_sha256","wrong"),("student_checkpoint_sha256","wrong"),
                              ("student_iteration",0),("camera_contract",{}),("student_decoder_contract","wrong")):
                bad=copy.deepcopy(manifest);bad[key]=value
                with self.subTest(key=key),self.assertRaises(ValueError):check_decoder(out,bad)
            for name in ("student_decoder.onnx","student_decoder.json"):
                bad=copy.deepcopy(manifest);bad["deployment_hashes"][name]="wrong"
                with self.assertRaises(ValueError):check_decoder(out,bad)
                bad=copy.deepcopy(manifest);del bad["deployment_hashes"][name]
                with self.assertRaises(ValueError):check_decoder(out,bad)
            with (out/"student_decoder.onnx").open("ab") as f:f.write(b"tamper")
            with self.assertRaises(ValueError):check_decoder(out,manifest)

    def test_optional_decoder_failure(self):
        import types
        from optional_student_decoder import add_optional_decoder
        def fail(checkpoint,output):
            Path(output,"student_decoder.onnx").write_bytes(b"partial")
            raise SystemExit("parity failed")
        with tempfile.TemporaryDirectory() as d, patch.dict(sys.modules,{"gast_student_decoder":types.SimpleNamespace(export_decoder=fail)}):
            with self.assertWarns(RuntimeWarning):
                fields,files=add_optional_decoder("unused",d)
            self.assertEqual(fields["student_decoder_status"],"unavailable")
            self.assertNotIn("student_decoder_contract",fields)
            self.assertEqual(files,[]);self.assertFalse(Path(d,"student_decoder.onnx").exists())

    def test_metrics_masks(self):
        p=np.zeros((187,6));y=p.copy();m=p.astype(bool)
        self.assertIsNone(offline.metrics(p,y,m)["height"]["mae_m"])
        m[:2,0]=True;p[0,0]=.2;p[1,0]=.4
        m[:3,2]=True;p[:2,2]=1;y[1:3,2]=1
        result=offline.metrics(p,y,m)
        self.assertAlmostEqual(result["height"]["mae_m"],.3)
        self.assertAlmostEqual(result["gap"]["iou"],1/3)
        self.assertEqual(result["gap"]["cells"],3)

    def test_roundtrip_offline(self):
        record={"schema":"gast.decoder.frame.v1","replica":1,"sequence":"1","input_stamp_ms":"100","observer_ms":"110"}
        record.update({k:np.zeros(shape).flatten().tolist() for k,shape in offline.SHAPES.items()})
        with tempfile.TemporaryDirectory() as d:
            p=Path(d,"frame_1.json");p.write_text(json.dumps(record))
            frame=offline.read_frame(p)
            self.assertEqual(frame["frames"].shape,(4,2,45,80))
            out=Path(d,"view.html");offline.render([frame],out)
            self.assertIn("NOT Pilot",out.read_text())
            with self.assertRaises(FileExistsError):offline.render([frame],out)
            record["hidden"]=[float("nan")]*6116;p.write_text(json.dumps(record))
            with self.assertRaises(ValueError):offline.read_frame(p)
            del record["frames"];p.write_text(json.dumps(record))
            with self.assertRaises(ValueError):offline.read_frame(p)

if __name__=="__main__":unittest.main()
