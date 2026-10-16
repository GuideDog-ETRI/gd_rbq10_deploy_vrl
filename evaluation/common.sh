# Sourced by the evaluation scripts. One evaluation owns the simulator at a time.
EVAL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$EVAL_DIR/.." && pwd)"
RUNNER="$EVAL_DIR/sim_trial_eval.py"
mkdir -p "$REPO/logs"
PY_ISAAC="apptainer exec /home/user/workspace/gd_lab_isaaclab.sif /home/user/workspace/venv_apptainer/bin/python"
FFMPEG="apptainer exec /home/user/workspace/gd_lab_isaaclab.sif ffmpeg"
XTKEY="$REPO/build/xtkey"
[ -x "$XTKEY" ] || gcc -O2 -o "$XTKEY" "$EVAL_DIR/xtkey.c" -lX11 /usr/lib/x86_64-linux-gnu/libXtst.so.6
export PATH="$EVAL_DIR/bin:$PATH"   # gnome-terminal shim: launch tabs in the background

sim_busy() { pgrep -x CAMEL-Pilot >/dev/null || pgrep -x CAMEL-Console >/dev/null; }

stop_sim() {  # stop the launch evaluation started (the marker names its bundle)
  [ -f "$REPO/logs/owned-launch" ] || return 0
  bash "$REPO/scripts/common/run_sim_vrl.sh" stop >/dev/null 2>&1 || true
  rm -f "$REPO/logs/owned-launch"
  sleep 3
}

# launch <run_sim.sh> [args...] : fresh launch, wait for the synced simulator and its window
launch() {
  stop_sim
  bash "$@" > "$REPO/logs/eval_last_launch.log" 2>&1
  for i in $(seq 1 60); do
    docker exec rbq-sim-vrl sh -c 'pgrep -f "^./Mujoco(Gast|Vrl)Sync"' >/dev/null 2>&1 && break; sleep 2
  done
  sleep 10
}

mujoco_window() {
  local w; w=$(xwininfo -display :2 -root -tree 2>/dev/null | grep '"MuJoCo' | awk '{print $1}' | head -1)
  echo "${w:-0x200007}"
}

vision_window() {  # "<display> <xid>" of the BT0..3 Depth / IR viewer (tools/vision-viewer), empty when not open.
  # MuJoCo draws on the container's :2, but the viewer opens from gnome-terminal on the desktop ($DISPLAY, :0).
  local d w
  for d in :2 "${DISPLAY:-:0}"; do
    w=$(xwininfo -display "$d" -root -tree 2>/dev/null | grep '"Terrain diagnostic - BT0..3 Depth / IR' | awk '{print $1}' | tail -1)
    [ -n "$w" ] && { echo "$d $w"; return; }
  done
}

# record <out_dir> : follow camera on, start video + 150 Hz state probe (REC/PROBE pids)
record_start() {
  local out=$1 secs=$2 w; w=$(mujoco_window)
  "$XTKEY" :2 "$w" bracketright; sleep 1
  local v vd; read -r vd v <<<"$(vision_window)"
  gst-launch-1.0 -e ximagesrc display-name=:2 xid=$((w)) use-damage=false ! video/x-raw,framerate=15/1 ! videoconvert ! videoscale ! video/x-raw,width=1258,height=480 ! vp8enc deadline=1 target-bitrate=3000000 ! webmmux ! filesink location="$out/walk.webm" > "$out/record.log" 2>&1 &
  REC=$!; REC_V=
  if [ "${REC_VISION:-1}" = 1 ] && [ -n "$v" ]; then
    # The BT0..3 Depth / IR diagnostic window (640x480, letterboxed) as a second file; record_stop puts it right of
    # MuJoCo (1258x480) in walk.mp4. One gst compositor here fails: webmmux refuses its caps change at start.
    gst-launch-1.0 -e ximagesrc display-name=$vd xid=$((v)) use-damage=false ! video/x-raw,framerate=15/1 ! videoconvert ! videoscale add-borders=true ! video/x-raw,width=640,height=480 ! vp8enc deadline=1 target-bitrate=2000000 ! webmmux ! filesink location="$out/walk_vision.webm" > "$out/record_vision.log" 2>&1 &
    REC_V=$!
  fi
  "$REPO/build/tools/sim-state-probe" "$secs" > "$out/state.jsonl" 2> "$out/state.err" &
  PROBE=$!
  sleep 1
}

record_stop() {
  local out=$1
  sleep 1; kill -INT $REC ${REC_V:-}; wait $REC ${REC_V:-} || true; kill $PROBE 2>/dev/null || true
  if [ -s "$out/walk_vision.webm" ]; then
    $FFMPEG -y -loglevel error -i "$out/walk.webm" -i "$out/walk_vision.webm" -filter_complex "[0:v][1:v]hstack=inputs=2:shortest=1" \
      -c:v libx264 -pix_fmt yuv420p -movflags +faststart "$out/walk.mp4"
  else
    $FFMPEG -y -loglevel error -i "$out/walk.webm" -c:v libx264 -pix_fmt yuv420p -movflags +faststart "$out/walk.mp4"
  fi
}

start_stand() {
  local out=$1
  python3 "$RUNNER" --command start --seconds 10 --output "$out/start.json" > "$out/start.log" 2>&1 || { echo start failed; tail -3 "$out/start.log"; return 1; }
  python3 "$RUNNER" --command stand --seconds 12 --output "$out/stand.json" > "$out/stand.log" 2>&1 || { echo stand failed; return 1; }
}
