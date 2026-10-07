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

## 수정 회차 2 정책 (2026-10-08)

- gast_checkpoint.py는 기존 출력 폴더를 거부한다. 동일 이름 재export 대신 새 이름을 사용한다.
  이 동작은 회차1에서 추가되었으며 유지한다. pipefail 없는 외부 스크립트는 이전 산출물을 성공으로
  오인할 수 있으므로 호출자는 export 종료 코드와 새 manifest를 확인해야 한다.
- 디코더 export 실패는 회차1과 달리 정책 bundle 생성을 중단하지 않는다.
  actor/student 본체 parity 실패는 여전히 치명적이다. 보조 decoder만 실패하면
  RuntimeWarning, student_decoder_status=unavailable, student_decoder_error를 manifest에 남기고
  decoder contract/hashes 없이 정상 정책 bundle을 완성한다. 새로 생긴 불완전 decoder 파일만 제거한다.
  --require-decoder나 decoder 뷰어는 이 bundle을 거부한다.
- 직접 C++ 실행도 manifest에 decoder contract와 두 파일 hash 등록이 없으면 DDS 초기화 전 거부한다.
  등록된 hash의 실제 일치는 Python run_viewer.py가 검사한다. C++ 직접 실행은 이 차이를 경고한다.
- --record-max-frames 기본1024(1..100000), --record-max-mb 기본512(1..10240 MiB).
  JSON 파일만의 총 byte/프레임 상한이며 복사한 manifest/metadata와 --save PNG는 제외한다.
  별도 writer thread, 대기 queue 2개, 가득 차면 새 frame drop. 상한 도달하면 기록은 멈추고 뷰어는 계속된다.
  종료 시 written/bytes/dropped/capped/error를 출력한다. 디스크 오류는 비정상 종료한다.
  PNG 저장에는 이 상한이 없으므로 --save 사용 시 유한 --seconds를 반드시 지정한다.
- offline 표시도 CW90, depth TURBO/invalid magenta, IR gray, 지형 TURBO/VIRIDIS를 사용한다.
  --height-min/--height-max/--dim-visibility를 C++처럼 지원하며 dim은 기본 꺼짐이다.
  셀 좌표는 display_cell이 만든 표를 실제 JavaScript 그리기가 사용한다.
- 기록의 자세 부재 표기는 capture_pose_status="unavailable"이다(pose=null 필드 아님).
- 실행 전용 CPU 검증:
  `bash tests/run_student_decoder_tests.sh`
  호스트 C++ OpenCV .406 기록 → 컨테이너 Python 읽기, 컨테이너가 만든 실제 HTML → 호스트 Node 검사.
  컨테이너에는 호스트 OpenCV .406과 node가 없어 하나의 컨테이너 안에서 모두 실행할 수 없다.
  wrapper는 새 /tmp/decoder-cpu-tests.*에 증거를 보존하고 GPU/DDS/GUI는 사용하지 않는다.
  별도 빌드 위치는 DECODER_TEST_BUILD, 학습 소스는 GD_LAB_TRAIN_ROOT로 지정할 수 있다.
  Python만 재실행 시 DECODER_CPP_RECORD_DIR=<C++ 증거 폴더>, DECODER_HTML_TEST_OUT=<새 HTML>를 전달한다.
- B main 병합은 학생28000 평가가 기존 main exporter 사용을 끝낸 뒤에만 검토한다.
  현재 push/merge 하지 않는다.
