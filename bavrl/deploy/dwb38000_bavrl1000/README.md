# DWB-38000 + BAVRL-1000 전용 배포

MuJoCo 시뮬레이션 전용이다. 실제 로봇에서 사용하지 않는다.
고정 블라인드 교사 DWB-38000에 BAVRL 잔차 정책을 1,000회 학습한 모델이다.

저장소 루트에서 실행:

```bash
bash bavrl/deploy/dwb38000_bavrl1000/run_sim.sh
```

종료:

```bash
bash bavrl/deploy/dwb38000_bavrl1000/stop_sim.sh
```

모델은 `resources/policy/bavrl/dwb38000_bavrl1000_20260930/`에 보관한다.
환경변수로 다른 정책이 지정되어 있어도 이 진입점에서는 BAVRL-1000으로 고정한다.
공통 실행기 `scripts/run_sim_vrl.sh` 및 공통 MuJoCo 지형을 사용하며,
기존 BAVRL 추론 코드는 `bavrl/src/`를 사용한다. 모델이나 공통 소스를 중복 복사하지 않는다.
이 디렉토리는 별도 실행 환경/동시 실행 인스턴스가 아니다. 공통 실행기의
Pilot·Console·viewer 정리 및 포트를 공유하므로 다른 배포 실험과 동시에 실행하지 않는다.
실행은 시뮬레이터를 기동하며 자동 WALK 명령을 보내지 않는다.

1,000회 모델은 평지 짧은 시험만 수행했다. 방향 편차와 영상 지연 문제가 남아 있으며
계단·갭 통과 성능은 미검증이다. 상세 기록은 `bavrl/README.md` 참조.
