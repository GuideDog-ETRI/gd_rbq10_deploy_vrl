# rbq_low_level.cpp — 벤더 레퍼런스, 대조용

`RBQ/rbq_sdk/cpp/example/src/rbq_low_level.cpp` 무수정 사본 (RBQ SDK v1.19.47).
`rbq_sdk/` 의 lib/include 를 벤더링한 것과 같은 이유로 여기 있다 — 다만 이것은
**빌드하지 않는다.** 링크된 타깃이 없다.

## 왜 사본을 두는가

`run_name == "rbq10"` 으로 obs 를 조립하는 주체가 이 파일이다. 벤더 바이너리가
아니다 — `bin/{RLWalk,QuadWalk,Motion,GUI,WalkReady}` 와 `librbq_sdk.so` 어디에도
`rbq10` 문자열이 없다(바이트 검색). RLWalk 은 depth-parkour 전용이고, QuadWalk 은
blind 정책 러너이지만 정책을 Qt 내장 리소스에서 풀어 쓰고 DDS gait_id 로만 고른다.
공식 배포 경로는 이 예제를 빌드해 `-p <policyDir>` 로 돌리는 것이다
(rainbowrobotics.github.io/RBQ → SDK → Low Level).

그래서 이 파일의 `:270-358` 이 **rbq_lab 정책의 obs 계약 원문**이다.
`pilot/src/PolicyBackend.cpp` 의 VendorBackend 는 그것을 term 단위로 옮긴 것이고,
학습측을 의심할 일이 생기면 대조할 곳이 여기다.

## 왜 빌드하지 않는가

돌릴 수 있지만 돌릴 이유가 없다.

- **같은 processId 20 을 쓴다.** CAMEL-Pilot 과 공존할 수 없다 — 둘이 동시에
  `rt/rbq/ref/motion/_20` 을 쏘면 어느 쪽 지령도 온전히 안 들어간다.
- **안전장치가 없다.** Damp E-stop, 피드백/tilt/is_fall 워치독, 텔레메트리,
  핸드오프 3규칙 전부 없다. 정지 수단은 'z'(접기)와 Ctrl+C 뿐이고 Ctrl+C 는
  발행을 끊을 뿐 감쇠를 넣지 않는다.
- **명령을 `rt/rbq/joy` 로만 받는다.** 발행자가 없으면 `axes().at(1)` 이
  `out_of_range` 를 던지고(:257), 그 예외가 :390 에서 잡혀 'c' 를 누른 순간
  Idle 로 떨어진다. Pilot 은 콘솔에서 명령을 받으므로 RC 송신기에 매이지 않는다.

정책만 필요하면 Pilot 이 싣는다:

    RBQ_POLICY_FILE=rbq10 scripts/run.sh      # resources/policy/rbq10/

## 다시 빌드해야 한다면

한 줄이면 된다 (벤더링한 것만으로 빌드된다 — SDK 설치 불필요):

    g++ -std=c++17 -O2 -DAPP_NAME='"rbq_low_level"' \
        -I extern/rbq_sdk/include -I extern/rbq_sdk/include/ddscxx \
        -I extern/rbq_sdk/include/ddsc -I extern/onnxruntime/include \
        -I /usr/include/eigen3 \
        extern/rbq_sdk/example/rbq_low_level.cpp \
        -L extern/rbq_sdk/lib -L extern/onnxruntime/lib \
        -lrbq_sdk -lddscxx -lddsc -lonnxruntime -lpthread -lrt \
        -Wl,-rpath,'$ORIGIN' -o /tmp/rbq_low_level

그때는 **Pilot 을 먼저 내린다.**
