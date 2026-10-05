"""Static lifecycle regression checks; never launch or stop the simulator."""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SimulationLaunchers(unittest.TestCase):
    def test_model_wrappers(self):
        # Discover new method directories too, rather than hard-coding aliases.
        wrappers = [p for p in ROOT.rglob('*sim*.sh')
                    if p.relative_to(ROOT).parts[0] not in
                    {'scripts', 'simulation', 'docker', 'logs', 'extern'}
                    and not p.relative_to(ROOT).parts[0].startswith(('.', 'build'))]
        self.assertTrue(wrappers)
        for path in wrappers:
            with self.subTest(path=str(path.relative_to(ROOT))):
                self.assertTrue(os.access(path, os.X_OK), 'chmod 755 required')
                subprocess.run(['bash', '-n', str(path)], check=True)
                source = path.read_text()
                self.assertNotIn('gnome-terminal', source)
                self.assertNotIn('pkill ', source)
                if path.name.startswith('stop'):
                    self.assertIn('run_sim.sh', source)
                    self.assertIn('stop', source)
                else:
                    self.assertIn('scripts/run_sim', source)
                    self.assertIn('"$@"', source)
                    if path.relative_to(ROOT).parts[0] != 'dwb':
                        self.assertIn('scripts/run_sim_vrl.sh', source)

    def test_shared_window_cleanup(self):
        for name in ('run_sim.sh', 'run_sim_vrl.sh'):
            source = (ROOT / 'scripts' / name).read_text()
            self.assertIn('source "$SCRIPT_DIR/sim_windows.sh"', source)
            self.assertIn('--role="$SIM_WINDOW_ROLE"', source)
            stop = source.split('# ---- stop:', 1)[1].split('exit 0', 1)[0]
            self.assertIn('close_sim_windows', stop)


if __name__ == '__main__':
    unittest.main()
