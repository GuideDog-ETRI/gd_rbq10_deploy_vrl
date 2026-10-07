"""Static lifecycle regression checks; never launch or stop the simulator."""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SimulationLaunchers(unittest.TestCase):
    def test_bundle_wrappers(self):
        # Every scripts/<method>/.../run_sim.sh only names its bundle and sources the shared launcher.
        wrappers = [p for p in (ROOT / 'scripts').rglob('run_sim.sh') if p.parent.name != 'common']
        self.assertTrue(wrappers)
        for path in wrappers:
            with self.subTest(path=str(path.relative_to(ROOT))):
                self.assertTrue(os.access(path, os.X_OK), 'chmod 755 required')
                subprocess.run(['bash', '-n', str(path)], check=True)
                source = path.read_text()
                self.assertNotIn('gnome-terminal', source)
                self.assertNotIn('pkill ', source)
                self.assertIn('common/launch.sh', source)
                self.assertIn('BUNDLE=', source)
                self.assertIn('SYNC_APP=', source)

    def test_bundles_exist(self):
        for path in (ROOT / 'scripts').rglob('run_sim.sh'):
            source = path.read_text()
            line = next((l for l in source.splitlines() if l.startswith('BUNDLE=')), None)
            if line is None or '$' in line:
                continue  # the GAST teacher launcher takes its bundle as an argument
            with self.subTest(path=str(path.relative_to(ROOT))):
                self.assertTrue((ROOT / 'resources/policy' / line.split('=', 1)[1] / 'policy_vrl.onnx').is_file())

    def test_shared_window_cleanup(self):
        source = (ROOT / 'scripts/common/run_sim_vrl.sh').read_text()
        self.assertIn('source "$SCRIPT_DIR/sim_windows.sh"', source)
        self.assertIn('--role="$SIM_WINDOW_ROLE"', source)
        stop = source.split('# ---- stop:', 1)[1].split('exit 0', 1)[0]
        self.assertIn('close_sim_windows', stop)

    def test_no_default_policy(self):
        # A launcher that forgot its bundle must fail, not walk another (or a blind) model.
        source = (ROOT / 'scripts/common/run_sim_vrl.sh').read_text()
        self.assertNotIn('DEFAULT_VRL_POLICY', source)
        self.assertNotIn('ls -t', source)


if __name__ == '__main__':
    unittest.main()
