# 학생 디코더 상세 설계 — 2026-10-08
## 기준과 격리
배포 main da2d87a / 학습 main a1e9655. 구현은 archive/codex_worktrees/student_decoder.
기본 checkout, merge_gd_lab, 기존 resources/policy, records, 원격 서버는 수정하지 않는다.
GPU/Isaac/Pilot 실행 및 push 없음. 이 문서는 Pilot 변경 승인이 아니다.

## 목적과 의미
왼쪽은 복제 학생이 실제 encode에 넣은 정규화 depth/IR 텐서, 오른쪽은 spatial_head의 보조 예측.
원본 센서 이미지나 Pilot 내부 기억 자체가 아니다. replica 시작 시점·드롭·리셋은 Pilot과 다를 수 있다.
GAST만 지원하며 loopback simulation 가드를 유지한다. latent 32개를 지형으로 역변환하는 것이 아니라
187칸 × 32차원 spatial memory에 학습된 Linear(32,6)를 적용한다.
hidden 6116 중 앞 5984를 사용. 출력 [1,187,6], 채널 0 회귀(m), 1..5 sigmoid.
높이는 scanner_z-ground_z-0.5이며 지면 월드 z가 아니다.
행 y=-0.5..0.5, 열 x=-0.8..0.8, flat=row*17+col. 화면 위=+x, 왼쪽=+y.

## B.3 항목별 결정
1. 정답: 학습 geometry.targets 정의를 그대로 참조한다. 4cm 차이 임계, gap은 semantic terrain label,
   missing depth가 아니다. 높이 MAE는 visible AND scan-valid, gap IoU는 known AND valid,
   edge는 유효 이웃쌍, support는 x/y 이웃과 gap-known을 분모로 한다. 빈 분모는 null.
   TerrainScan.observeFull만으로 teacher visibility나 semantic gap-known을 얻었다고 간주하지 않는다.
   현재 DebugSnapshot에는 capture pose/world z/semantic gap/teacher visibility가 없어 온라인 oracle 비교는
   미확인 상태로 표시하며 계산하지 않는다. 임의 현재 자세로 정답을 맞추지 않는다.
   오프라인 aligned targets[187,6]와 masks[187,6]를 공급한 경우에만 위 마스크별 MAE/IoU 집계.
2. 복제본: 현재 방식 유지. 모든 화면·기록에 replica 명시. Pilot 진단 DDS는 승인 대기.
   제안: 기본 off, bounded single-slot snapshot, worker에서 serialize/publish, 제어 스레드 I/O 금지.
   off 비용 0은 실제 측정 전 보장하지 않는다. 소스 건드리지 않고 승인 요청으로 남긴다.
3. GAVD: hidden-only decode 불가. frames+hidden+명시적 입력 계약을 가진 별도 보조 출력 ONNX가 필요.
   RVLD: hazard scalar만 가능, 가짜 6채널 지도 생성 금지. 이번 구현은 둘 다 명확히 unsupported.
4. 기록: PNG 외 machine-readable OpenCV FileStorage JSON frame v1.
   seq, input_stamp_ms(monotonic), observer_ms, replica=true, frames[4,2,45,80],
   hidden[6116], latent[32], decoded[187,6]. 원본 pose 없는 파일은 capture_pose_status="unavailable".
   파일명 sequence 기반, 전용 새 출력 폴더를 요구하고 덮어쓰기 거부.
   수정 회차2: bounded queue(2), writer thread, 1024 frame/512 MiB JSON 기본 상한. drop 및 cap 집계.
   offline Python CPU reader는 pickle 없이 읽고 shape/finite 검증, 시간 점프·누락 검출.
   RL replay JSON과 동일 형식이라고 위장하지 않는다. 향후 schema adapter로 연결.
5. export: export_decoder 함수 분리, seed42/CPU 단일 thread parity, metadata에 source SHA256/ONNX SHA256/
   camera contract/grid/channel/contract version. 새 gast_checkpoint export에 자동 포함하고 manifest hash에 등록.
   check_gast_bundle은 새 decoder contract를 검증하며 legacy decoder 없음은 명시적으로 구분.
   기존 묶음은 이번에 변경하지 않는다. backfill은 새 --output-dir 복사본만 허용.
   수정 회차2: 정책 parity 통과 후 decoder만 실패하면 경고와 unavailable/error metadata로 bundle 생성은 계속한다.
   decoder contract와 불완전 decoder 파일은 남기지 않는다. 본체 parity 실패는 여전히 중단한다.
6. topics: 기존 VisionTopics 포맷과 우선순위 유지. tools만 파서 재사용. 공용 parser/Pilot/launch 동작은 바꾸지 않는다.
   AppImage 경로 정책은 기존 RBQ_VISION_TOPICS 명시 경로를 유지하며 실기 가드 해제 없음.
7. 실기: pose 출처/clock domain/sensor calibration/epoch가 확인된 뒤 별도 승인. 이번에는 설계만.
8. 표시: height 기본 [-0.25,+0.35]m, 옵션으로 조정; 낮은 visibility 칸 dim(마스크와 다름).
   발 위치는 capture 동기 pose/FK 없으므로 가짜 overlay 금지. 최근 N frame offline 추적.
   null/nonfinite는 invalid 색과 오류로 표시. 추정값을 실제 안전한 foothold로 표현하지 않는다.
9. 테스트: CPU tensor/ONNX parity, grid 양 끝/방향, mask별 metrics, JSON roundtrip/오류/누락,
   VisionTopics 기본/파일/env/{i} 파싱. C++ 별도 build, 제한된 병렬수. 실제 DDS 구독/시뮬레이션은 이번 검증에서 제외.

## 인터페이스와 Pilot 영향
export/gast_student_decoder.py --bundle ... --checkpoint ... --output-dir <새 경로>
tools 학생 뷰어 --record <새 디렉터리> --height-min ... --height-max ... --dim-visibility
tools/student_decoder/offline.py <record-dir> [--output <새 HTML>] : 정적 HTML CPU 출력, 네트워크/GPU 없음.
기존 VisionStudentThread의 enableDebugSnapshot만 사용한다. 공용 thread/header 및 Pilot control code 수정 없음.
학습 weight와 actor ONNX 수정 없음. 파일 쓰기는 툴 프로세스에서만 수행.
온라인 GT·발 overlay·Pilot 실제 hidden DDS는 데이터 계약과 승인 미충족으로 분리 보류.

## 승인 경계
Pilot 진단 토픽, DebugSnapshot capture-pose 확장 등 공용 경로 수정은 사용자 승인 후 별도 작업.
문서대로 독립 뷰어/export/CPU tests는 현재 구현 승인 범위다.
