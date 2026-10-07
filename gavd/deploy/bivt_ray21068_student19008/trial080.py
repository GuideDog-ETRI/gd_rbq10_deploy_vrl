"""Reuse the previously tested fall-only loopback trial against the common GAVD runtime."""
import importlib.util
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
path = ROOT / 'gast/runtime/experiments/runners/sim_trial_080_fall_codex.py'
spec = importlib.util.spec_from_file_location('bounded_trial', path)
trial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trial)
trial.ROOT = ROOT
if __name__ == '__main__':
    pid = trial.verify_sim()
    env = Path(f'/proc/{pid}/environ').read_bytes().split(b'\0')
    assert b'RBQ_POLICY_FILE=gavd/bivt_ray21068_student19008_20261007/policy_vrl.onnx' in env
    trial.main()
