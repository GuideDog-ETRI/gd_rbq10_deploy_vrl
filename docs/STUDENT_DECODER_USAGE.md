# GAST 학생 디코더 진단 도구

이 도구는 읽기 전용 **복제 학생**의 입력과 학습된 spatial head 예측을 표시한다.
Pilot 내부 hidden, 원본 센서 영상, 실제 지형 정답을 표시한다고 해석하면 안 된다.
실기 가드는 그대로이며 GAST loopback simulator에만 붙일 수 있다.

## 준비 (CPU, 새 출력만)

새 gast_checkpoint.py export는 decoder ONNX/JSON 및 manifest hashes를 포함한다.
기존 bundle에는 새 복사본을 만든다. source는 변경하지 않는다:

    GD_LAB_TRAIN_ROOT=<학습소스> python export/copy_with_student_decoder.py --source <기존 bundle 절대경로> --output <아직 없는 복사본 절대경로>

GPU 없이 head만 별도 내보내려면 gast_student_decoder.py --bundle gast/<name>
--checkpoint <pt> --output-dir <새 디렉터리>를 사용한다.
source checkpoint SHA256, ONNX SHA256, camera contract, iteration, channels, grid, CPU parity를 기록한다.
GAVD/RVLD checkpoint는 거부한다.

## 실험 종료 후에만 수동 실행

현재 승인된 작업에서는 아래 명령을 실행하지 않았다. Pilot 또는 시뮬레이터를 자동 시작하지 않는다.

    python tools/student_decoder/run_viewer.py --bundle <검증된 새 bundle> --binary <별도 build>/tools/student-decoder-viewer -- --interface lo --sim --headless --record <새 기록 디렉터리> --seconds 20 --dim-visibility

헬퍼는 manifest/decoder hashes·parity metadata를 검증하고 nice 19로 실행한다.
별도 새 binary를 명시해야 하며 기존 checkout의 build를 쓰지 않는다.
--save <새 폴더>는 PNG, --record <새 폴더>는 machine-readable JSON이다.
--height-min, --height-max는 색상 범위만 바꾸고 모델 출력은 바꾸지 않는다.
기존 출력 폴더 재사용은 거부한다.

## 오프라인 재생

    python tools/student_decoder/offline.py <기록 폴더> --start 0 --limit 128 --output <새 파일.html>

DDS/GPU/웹서버 없이 정적 HTML을 만든다. 기본 128, 최대 256 frame 구간이다.
순번 누락은 콘솔에 표시하며 각 frame의 입력 시각은 observer monotonic clock이다.
행 y=-0.5..0.5, 열 x=-0.8..0.8; 화면 위가 전방이다.
원본 capture pose가 없으므로 world 높이·foot overlay·oracle 비교를 하지 않는다.
별도로 동기화된 geometry.targets 결과를 공급할 때만 offline.metrics를 사용한다.
mask별 cells/union이 분모이며 빈 분모는 null이다.

## 승인 대기

Pilot hidden/pose 진단 DDS, 공용 DebugSnapshot 확장, 온라인 oracle 정답, 실기 지원은 구현하지 않았다.
GAVD는 frames+hidden 보조 출력 경로, RVLD는 hazard만 가능한 별도 설계가 필요하다.
관측 차원이 같다는 이유로 GAST decoder를 다른 학생에 붙이지 않는다.
상세 근거: CODEX_STUDENT_DECODER_DESIGN_20261008.md.
