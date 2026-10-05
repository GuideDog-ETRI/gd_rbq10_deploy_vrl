# 배포 구조 — 2026-09-30

- `pilot/`: 공통 제어·안전·모델 선택 및 vendor/metadata Actor 추론.
- `dwb/`: 기존 블라인드 추론 backend와 실행기.
- `rvld/`, `gavd/`: CNN–GRU / Attention 전용 실행기와 모델 연결 안내.
- `bavrl/`: 잔차 추론 backend·카메라 만료 규칙·실행기.
- `cvtt/`, `bivt/`: 교사 출처별 학생 연결 안내 (별도 deploy runtime 아님).
- `perception/common/`: 공통 ONNX 학생 스레드, 프레임 큐, age/latent 계약.
  CNN-GRU와 Attention은 metadata로 구분하며 동일 ONNX 런타임을 공유합니다.
  동일한 추론 코드를 별도 backend 디렉터리에 복제하지 않습니다.
- `simulation/mujoco/`: Docker 및 동기 영상 MuJoCo 연동.
- `simulation/terrains/`: 기존 평지·계단·갭 XML. 지형 내용은 그대로 보존.
- `experiments/runners/`: 보행·영상 비교 실행.
- `experiments/analysis/`: 오프라인 결과 분석.
- `experiments/isaac_eval/`: 기존 Isaac 평가 도구.
- `tests/`: C++ 영상 계약 단위 테스트.
- `scripts/export_safe_vrl_pair.py`: 안전한 모델 쌍 export.
- `docs/experiments/`: 기존 실험 기록.
- `resources/policy/{dwb,rvld,gavd,bavrl}/`: 약어별 모델. 개별 bundle명과 원본 manifest/가중치 보존.

## 공통 시험 진입점

**`scripts/run_sim_vrl.sh`를 공통 배포 시험 진입점으로 유지합니다.**
기존 모델 선택, 동기 영상, 제어 설정을 유지하고 새 지형/시뮬레이터 경로만 반영했습니다.
`rvld/run_sim.sh`, `gavd/run_sim.sh`, `bavrl/run_sim.sh`가 이 공통 스크립트를 호출합니다.
`dwb/run_sim.sh`는 기존 블라인드용 `scripts/run_sim.sh`를 호출합니다.
실험 실행은 별도 사용자 요청 없이 하지 않습니다.

별도 빌드: `cmake -S . -B build-layout-check -DBUILD_CONSOLE=OFF`.
기존 `build/`의 바이너리는 이번 검증으로 교체하지 않습니다. 다음 실행 전에 새 소스로 재빌드하세요.
실행 중 컨테이너의 이전 호스트 bind 경로는 그대로 남을 수 있으므로 새 경로 적용은 재생성 시 확인해야 합니다.

## 서버 역할

5090(10.254.90.20)은 학습·배포 겸용입니다.
다중 GPU 서버(10.77.32.231)는 기존 4카메라 렌더링 기반 교사 학습에 사용합니다.
소스 이동 목록은 `layout-migration.json`, 약어별 추가 이동은 `method-layout-migration.json`; 모델·로그 내용은 변경하지 않았습니다.
커밋과 push는 수행하지 않습니다.
