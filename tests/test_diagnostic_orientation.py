"""Display-only convention checks; no simulator commands are sent."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DiagnosticOrientation(unittest.TestCase):
    def test_shared_viewer(self):
        launcher = (ROOT / 'scripts/run_sim_vrl.sh').read_text()
        self.assertIn('/tools/vision-viewer', launcher)
        cmake = (ROOT / 'tools/CMakeLists.txt').read_text()
        self.assertIn('add_executable(vision-viewer src/student_input_viewer.cpp)', cmake)

    def test_clockwise_images_and_mask(self):
        source = (ROOT / 'tools/src/student_input_viewer.cpp').read_text()
        self.assertIn('cv::rotate(panel, rotated, cv::ROTATE_90_CLOCKWISE)', source)
        self.assertIn('cv::rotate(invalidDepth[cam], rotatedMask, cv::ROTATE_90_CLOCKWISE)', source)
        self.assertIn('kTileWidth = 180, kTileHeight = 320', source)
        policy = (ROOT / 'perception/common/VisionStudentThread.cpp').read_text()
        self.assertNotIn('ROTATE_90_CLOCKWISE', policy)


if __name__ == '__main__':
    unittest.main()
