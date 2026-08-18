pragma Singleton
import QtQuick

// 콘솔의 디자인 토큰.
//
// ── 원칙: 색은 오직 상태만 의미한다 ──────────────────────────────────────
// 크롬(탭·버튼·테두리·패널 제목)에서 채도를 전부 뺐다. 화면에서 채도가 있는
// 픽셀은 상태 LED / 로그 레벨 / Emergency / Robot Start 뿐이다.
//
// 이건 취향이 아니라 계기판의 안전 속성이다. 원본은 이미 빨간 막대가 많은데
// 그 위에 파란 액센트까지 얹으면 "색이 켜졌다"가 아무 의미도 갖지 못한다.
// 채도를 상태에만 남기면 눈이 상태로만 간다.
//
// 중립색은 순회색이 아니라 살짝 차갑게(청회색) 잡았다. 가공된 알루미늄 계기
// 베젤의 색이고, 덕분에 따뜻한 빨강/초록이 "신호"로 확실히 분리된다.
QtObject {
    // ── 중립 (의미 없음 — 전부 무채도) ───────────────────────────────────
    readonly property color ink:      "#14171a"   // 본문, 활성 크롬, 탭 밑줄
    readonly property color graphite: "#4a5157"   // 보조 텍스트, 라벨
    readonly property color mist:     "#8b9298"   // 흐린 텍스트, 비활성
    readonly property color rule:     "#e2e5e8"   // 헤어라인, 테두리
    readonly property color surface:  "#ffffff"   // 패널
    readonly property color ground:   "#f2f4f5"   // 앱 배경, 3D 뷰포트

    // ── 상태 (의미 있음) ─────────────────────────────────────────────────
    //
    // ⚠️ 2026-08-04 디자인 개편으로 **빨강의 의미가 바뀌었다.**
    //    이전: 꺼짐 = idle(빨강)  — 원본 콘솔의 관례를 그대로 옮긴 것
    //    현재: 꺼짐 = rule(회색),  빨강은 **고장 · EMERGENCY 전용**
    //
    // 바꾼 이유: 연결 전 화면이 빨강 막대 8개로 덮여서, 정작 진짜 이상이 났을 때
    // 빨강이 아무 신호도 되지 못했다. 꺼짐은 이상이 아니라 그냥 꺼짐이다.
    // 비활성(누를 수 없음)은 색이 아니라 opacity 0.45 로 표현한다 — 색을 하나 더
    // 쓰면 그 색도 의미를 요구하게 된다.
    readonly property color live: "#52c41a"   // 활성
    readonly property color idle: "#ff4d4f"   // 고장 · EMERGENCY
    readonly property color warn: "#d48806"   // 한계 근접, 편차 과다 (gold-7)

    // 막대의 중심/한계 눈금. rule 보다 진해야 트랙 위에서 보이고, mist 보다는
    // 연해야 값을 가리지 않는다.
    readonly property color tick: "#c9ced2"

    // 비활성 행의 불투명도. mist 로 흐리게 하는 대신 이걸 쓴다 —
    // mist(#8b9298) 에 opacity 를 겹치면 대비가 1.5:1 로 떨어져 야외에서 사라진다.
    readonly property real dimmed: 0.45

    // 로그 레벨 — 흰 배경 위 본문 글씨라 antd -8 계열을 쓴다.
    // -6 은 대비가 2:1 수준이라 작은 글씨 벽에서는 읽히지 않는다.
    readonly property color logError:   "#ff4d4f"
    readonly property color logWarning: "#ad6800"
    readonly property color logSuccess: "#237804"
    readonly property color logInfo:    "#262626"
    readonly property color logDebug:   "#8c8c8c"

    // ── 타이포 ────────────────────────────────────────────────────────────
    // IBM Plex — 기술 제품용으로 설계된 서체이고, Sans/Mono 가 같은 슈퍼패밀리라
    // 콘솔 전체가 한 벌로 읽힌다. 시스템에 없을 수 있어 qrc 로 동봉한다
    // (원본이 Roboto 를 동봉한 것과 같은 이유).
    readonly property string sans: "IBM Plex Sans"
    readonly property string mono: "IBM Plex Mono"

    // 계단식 스케일. 원본은 12/13/15/17/22 가 근거 없이 흩어져 있었다.
    // 1280x800 을 팔 길이에서 보는 콘솔이고 현장에서는 서서 보는 경우도 있어,
    // 웹 기본값보다 한 단계 크게 잡는다.
    //
    // 표가 들어오면서 아래쪽(10/11/13)이, 버튼 라벨이 분리되면서 15 가 늘었다.
    // 계단이 촘촘해 보이지만 각 칸에 역할이 하나씩 있다 — 크기가 곧 종류다.
    readonly property int fsNano:    10   // 표 열 머리글
    readonly property int fsLabel:   11   // 패널/필드 라벨
    readonly property int fsMicro:   12   // 탭 라벨, 상태 칩
    readonly property int fsData:    13   // 표 숫자, 크롬 텍스트
    readonly property int fsSmall:   14   // 데이터, 옵션 라벨
    readonly property int fsAction:  15   // 버튼 라벨 (EMERGENCY, ROBOT START)
    readonly property int fsBody:    16   // 사이드바 모드 라벨, 본문
    readonly property int fsLarge:   20   // 강조
    readonly property int fsDisplay: 38   // ROBOT STATE — 화면의 앵커

    // ── 간격 ──────────────────────────────────────────────────────────────
    readonly property int gap:  9
    readonly property int pad:  14
    readonly property int pad2: 22
    readonly property int ledH: 4    // 상태 막대 두께 — 신호는 색이 내므로 얇게
}
