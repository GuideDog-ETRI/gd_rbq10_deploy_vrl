"""No GPU, DDS or simulator. Run with CPU numpy installed."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("decoder_offline",ROOT/"tools/student_decoder/offline.py")
offline=importlib.util.module_from_spec(spec);spec.loader.exec_module(offline)

class DecoderTests(unittest.TestCase):
    def test_grid_direction(self):
        self.assertEqual(offline.display_cell(0,0),(16,10))
        self.assertEqual(offline.display_cell(10,16),(0,0))
        self.assertEqual(offline.display_cell(5,8),(8,5))
        with self.assertRaises(ValueError):offline.display_cell(11,0)

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

if __name__=="__main__":unittest.main()
