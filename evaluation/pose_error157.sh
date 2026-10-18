# Shared UPDATE157 configuration. Sidecar defaults off; the review package opts into P2.
# Overrides: POSE_ERR_RW_XY, RW_YAW_DEG, SLIP, WHITE_XY, WHITE_YAW_DEG, DELAY_MS
# (each prefixed POSE_ERR_). All units match pose_error157.py.
pose157_setup() {
  local level=$1 log=$2 name value flag
  POSE_ARGS=
  case "$level" in
    off) unset RBQ_GAST_POSE_UDP RBQ_GAST_POSE_LOG RBQ_GAST_POSE_READY; export POSE_ERR=off; return ;;
    P0|P1|P2|P3) ;;
    *) echo "invalid POSE_ERR: $level" >&2; return 2 ;;
  esac
  export POSE_ERR=$level RBQ_GAST_POSE_UDP=${RBQ_GAST_POSE_UDP:-47611} RBQ_GAST_POSE_MAX_AGE_MS=${RBQ_GAST_POSE_MAX_AGE_MS:-150}
  export RBQ_GAST_POSE_LOG="$(dirname "$log")/pilot_pose.log" RBQ_GAST_POSE_READY="$log"
  printf -v POSE_ARGS '%q ' --pose-err "$level" --pose-log "$log" --pose-port "$RBQ_GAST_POSE_UDP"
  for name in RW_XY RW_YAW_DEG SLIP WHITE_XY WHITE_YAW_DEG DELAY_MS; do
    value="POSE_ERR_$name"; value=${!value:-}
    if [ -n "$value" ]; then
      flag="${name,,}"; flag=${flag//_/-}
      printf -v POSE_ARGS '%s%q %q ' "$POSE_ARGS" "--pose-$flag" "$value"
    fi
  done
  echo "[pose157] pose error: $level ($POSE_ARGS max_age_ms=$RBQ_GAST_POSE_MAX_AGE_MS; resolved parameters in $log)"
}
