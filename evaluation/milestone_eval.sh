#!/bin/bash
# milestone_eval.sh <target_update> : wait until the server GAST v2.1 teacher run reaches it, fetch its
# Top-1, export a GAST oracle bundle (export/gast_teacher_oracle.py) and evaluate it (eval_candidate.sh).
# GD_LAB_TRAIN_ROOT = the gd_lab_vrl gast/ tree the export checks parity against.
set -u
source "$(dirname "$0")/common.sh"
T=$1
SV="ssh -o BatchMode=yes -p 20022 bsseo@10.77.32.231"
W=/data/users/bsseo/gd_lab_vrl_worktrees/gast-v21-20261007/gast
RD=$W/logs/gast/arm4/2026-10-07_11-02-05_gast_gapclean_from_bivt_train
L=$W/logs/gast_v21_train_20261007.log
while true; do
  it=$($SV "grep -aoE 'Learning iteration [0-9]+' $L | tail -1 | grep -oE '[0-9]+'")
  tb=$($SV "grep -ac Traceback $L")
  [ "${tb:-0}" -gt 0 ] && { echo "TRACEBACK in GAST v2.1 run at $it"; exit 2; }
  [ -n "$it" ] && [ "$it" -ge "$T" ] && break
  sleep 600
done
top=$($SV "ls $RD/best_top5/ | grep _top1.pt")
CK="$REPO/records/checkpoints/gast_v21_at$T"; mkdir -p "$CK/params"
scp -q -P 20022 bsseo@10.77.32.231:$RD/best_top5/$top "$CK/" && scp -q -P 20022 bsseo@10.77.32.231:$RD/best_top5/leaderboard.json "$CK/"
for f in agent.yaml env.yaml; do scp -q -P 20022 bsseo@10.77.32.231:$RD/params/$f "$CK/params/"; done
sha=$(sha256sum "$CK/$top" | cut -d' ' -f1); n=${top%%_*}
echo "reached=$it top1=$top sha256=$sha"
python3 -c "import json;d=json.load(open('$CK/leaderboard.json'));print('leaderboard',[(e['iteration'],round(e['score'],4)) for e in d['entries']])"
GD_LAB_TRAIN_ROOT="${GD_LAB_TRAIN_ROOT:?set GD_LAB_TRAIN_ROOT to gd_lab_vrl/gast with the GAST teacher code}" \
  $PY_ISAAC "$REPO/export/gast_teacher_oracle.py" --checkpoint "$CK/$top" --sha256 "$sha" --name "gast_v21_${n}_oracle" --params "$CK/params" 2>&1 | grep -E '"parity_max_abs_error"|Error|assert'
bash "$EVAL_DIR/eval_candidate.sh" "gast_v21_${n}" 6 "$REPO/scripts/gast/teacher/run_sim.sh" "gast_v21_${n}_oracle"
