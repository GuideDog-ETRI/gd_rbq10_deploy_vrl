#!/bin/bash
# Export a GAST-Narrow v2.4 checkpoint (a6000 / Hangang) as a MuJoCo bundle and make its review folder.
#   bash make_v24_folder.sh <checkpoint.pt> <tag> [folder number]
#   e.g. bash make_v24_folder.sh ~/ckpt/v24c/412_top1.pt a6000_412 23  -> 23_narrow_v24_a6000_412/run_sim.sh
# The checkpoint's params/ (agent.yaml, env.yaml) must sit next to it or in a parent folder (export searches upward).
# The folder runs with NARROW=1 REAR=1 (tail Mid-360 + trunk occlusion in the head-LiDAR sidecar) and the turn course
# by default: bash <folder>/run_sim.sh [turn_narrow_force4|turn_rooms|narrow_only|force4_narrow]. The automatic stair
# forces follow the stairs of turn_narrow_force4 (sections.json force_x0); FORCE=0 turns them off.
set -euo pipefail
ck=$(realpath "${1:?checkpoint}"); tag=${2:?tag}; num=${3:-}
REPO=/home/user/gd_project/gd_rbq10_deploy_vrl
# Model classes from main (as the 371 / 602 exports): the gh200 line of gast/geometry.py (cuDNN off above 65,535 rows)
# cannot be traced to ONNX ("set_enabled_cudnn expects a bool"); the network itself is the same.
TRAIN_ROOT=${GD_LAB_TRAIN_ROOT:-/home/user/gd_project/gd_lab_vrl}
name=gast_narrow_v24_${tag}_$(date +%Y%m%d)
cd "$REPO"
sha=$(sha256sum "$ck" | cut -d' ' -f1)
apptainer exec /home/user/workspace/gd_lab_isaaclab.sif env MPLCONFIGDIR=/tmp/mpl_v24 GD_LAB_TRAIN_ROOT="$TRAIN_ROOT" \
  /home/user/workspace/venv_apptainer/bin/python export/gast_narrow_v2.py --checkpoint "$ck" --sha256 "$sha" --name "$name" \
  2>&1 | tail -1
mkdir -p "scripts/gast/teacher/$name"
cat > "scripts/gast/teacher/$name/run_sim_ir.sh" <<EOF
#!/usr/bin/env bash
# GAST-Narrow v2.4 $tag (in-place turns, rear Mid-360, backward passages): Ray+IR terrain + LiDAR map + margin.
here="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
exec "\$here/../run_sim.sh" $name "\$@"
EOF
dir="deploy/mujoco_review_20261018/${num:+${num}_}narrow_v24_${tag}"
mkdir -p "$dir"
cat > "$dir/run_sim.sh" <<EOF
#!/bin/bash
# narrow_v24_$tag -- GAST-Narrow v2.4 (from $(basename "$ck"), sha ${sha:0:8}): turn pockets / narrow passages.
#   bash run_sim.sh [turn_narrow_force4|turn_rooms|narrow_only|force4_narrow]     default turn_narrow_force4
#   LIDAR=both|ouster|mid360|off   MARGIN=0.05..0.15 (m, default 0.10)   REAR=1 (default; REAR= for no tail Mid-360)
HERE="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"; MODEL=narrow_v24_$tag; LAUNCHER=scripts/gast/teacher/$name/run_sim_ir.sh
NARROW=1; DEFAULT_COURSE=turn_narrow_force4; REAR=\${REAR-1}
source "\$HERE/../_lib.sh"; run_sim "\$@"
EOF
chmod +x "scripts/gast/teacher/$name/run_sim_ir.sh" "$dir/run_sim.sh"
echo "ready: bash $REPO/$dir/run_sim.sh   (bundle resources/policy/gast/$name, local only)"
