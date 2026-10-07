# BIVT-Ray 21068 → GAVD student 19008

90.111 서버에서 20,000 capture iteration으로 완료한 GAVD 결과의 배포 준비본입니다.
**CPU export/parity와 카메라 정적 검사만 완료했습니다. 시뮬레이터·GPU 추론·보행 시험은 실행하지 않았습니다.**

## 실행

```bash
cd /home/user/gd_project/gd_rbq10_deploy_vrl/gavd/deploy/bivt_ray21068_student19008
./run_sim.sh --check  # 파일 SHA 및 SDK 카메라 정적 검사만
./run_sim.sh          # 사용자가 별도 실행할 때만 빌드/시뮬레이터 시작
./stop_sim.sh         # 살아 있는 해당 모델 Pilot과 소유권이 일치할 때만 종료
```

자동 WALK 명령은 보내지 않습니다. 실행 후 콘솔에서 START → STAND → WALK를 선택합니다.
실행 중인 다른 Pilot/Console/시뮬레이터를 자동 종료하지 않습니다.
Pilot이 이미 종료됐거나 소유권 마커가 오래된 경우 stop은 안전하게 거절하므로 소유자를 확인해야 합니다.

## 모델과 출처

- 저장소 커밋: `3041279399c0c06dfa0094ef7df471accc68410f` (`vrl_models`에서 fetch).
- 기존 작업 트리를 수정하지 않고 별도 detached checkout 사용:
  `/home/user/gd_project/gd_lab_vrl_gavd_deploy_3041279`.
- 학생 패키지: 위 checkout의 `checkpoints/students/gavd/bivt_ray21068_student19008_20261007/`.
- 학생: `student/perception_19008.pt`, GAVD `grid_attention_v1`.
- 학생 SHA256: `975bea9794a2aadc504fe29153da7705a19ae63cf2f166ff3f9b64dc7663dafc`.
- 교사: 위 checkout의 `checkpoints/teachers/bivt/ray_gap_clean_vendor_new_top1_21068_20261005/teacher/21068_top1.pt`.
- 교사 SHA256: `fa397d22a949e24f74312b5270b5954a4ddb15c7451ea85ba8f64dbeeb7c9501`.
- 교사 agent 설정: 학생 패키지의 `params/teacher/agent.yaml`.
- 완료 run: `gavd21068_512env_20k_20261006_170023`, 512 env, seed 42, BPTT 16, LR 0.0003.
- 선정: 6,000회 이후 15개 후보 중 이전 1,000 capture iteration 평균 학습 프록시 최저 `0.19830153014747398`.
  식은 latent MSE + 0.5×hazard MSE + action MSE + 0.5×spatial loss.
  **보행 성공률 또는 held-out Top-1을 뜻하지 않습니다.** 전체 후보는 패키지 `selection.json` 참고.

## 배포 계약

- bundle: `/home/user/gd_project/gd_rbq10_deploy_vrl/resources/policy/gavd/bivt_ray21068_student19008_20261007/`.
- actor: `policy_vrl.onnx` 및 CPU TorchScript `policy_vrl.pt`.
- GAVD: `policy_vrl_student.onnx` 및 `policy_vrl_student.pt`.
- 학생 입력: frames `[1,4,2,45,80]`, hidden `[1,64]`; 출력 latent `[1,32]`, hidden `[1,64]`.
- hidden slot 63은 capture-clock frame age 초 단위(0–1로 제한)입니다.
- actor 입력: direct observation 46D, CENet history 230D, 학생 terrain latent 32D; action 12D.
- 공통 `VisionStudentThread` / `DreamVrlBackend` 사용. GAST 전용 6116D hidden 런타임을 사용하지 않습니다.
- 학생 대신 교사 ray encoder를 사용하는 환경변수 및 테스트 replay/plane, 카메라 우회 변수는 런처에서 제거합니다.
- 카메라 `vendor_new`, SDK 기본 `/home/user/gd_project/RBQ_vendor_new/RBQ-nightly`.
- 기존 지형 `/home/user/gd_project/gd_rbq10_deploy_vrl/simulation/terrains/vrl_progression` 사용; 새 지형 생성/변경 없음.
- 기존 공통 시뮬레이터 `rbq-sim-vrl`, loopback, payload +6kg, sync vision, history `repeat_first`.

## 검증 결과

- 패키지 `SHA256SUMS.txt`: 모든 항목 통과. 교사 해시도 일치.
- 저장된 GAVD 모델 구조 strict load 및 카메라 기하 검증 통과. 패키지 snapshot과 사용한 GAVD 모델 소스 동일.
- actor TorchScript↔ONNX 최대 절대오차 `5.7220458984375e-06`.
- 학생 TorchScript↔ONNX 최대 절대오차 `1.4901161193847656e-07`.
- 16회 CPU recurrent/age 입력 테스트: eager GAVD↔ONNX 최대 절대오차 `2.1606683731079102e-07`.
- 합성 영상 → 학생 latent → actor action 모두 유한값. 허용오차 atol=1e-5, rtol=1e-4.
- 실제 SDK와 policy 카메라 계약 `vendor_new` 일치. 셸 문법 검사 통과.
- CUDA 숨김, Apptainer `--nv` 미사용, CPUExecutionProvider 및 스레드 1 사용.
- 시뮬레이터, 빌드, GPU inference, 학습, 녹화는 하지 않았으며 기존 작업을 중단하지 않았습니다.
- `deployment_manifest.json`: 기존 공식 exporter 결과. `gavd_provenance.json`: 추가 CPU 검증과 파일 SHA.
- 배포 저장소 기준 HEAD `212eabafe30983e8add738703baf1ae079c8fc44` + 기존 미커밋 변경 보존.
  이 배포 폴더와 bundle은 로컬 추가이며 커밋/푸시하지 않았습니다.

## 재검증

`verify_export.py`는 GD_LAB_ROOT/PYTHONPATH를 위 격리 checkout으로 지정해 CPU 환경에서 실행합니다.
CPU PyTorch 환경은 `/home/user/workspace/gd_lab_isaaclab.sif` 내부의
`/home/user/workspace/venv_apptainer/bin/python`입니다(호스트에서는 이 Python 링크가 실행되지 않음).
일반 사용자는 `./run_sim.sh --check`만으로 파일 무결성과 카메라 계약을 확인할 수 있습니다.
