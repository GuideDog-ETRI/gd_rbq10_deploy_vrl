#pragma once

#include <cmath>

// 변경분 방출(diff emit)용 소형 헬퍼.
//
// 왜 필요한가
//   원본은 100ms 타이머마다 위젯 217곳을 무조건 갱신했다. QML 로 그대로 옮기면
//   프로퍼티 50여 개가 매 tick 무조건 NOTIFY 를 내고, 거기 걸린 바인딩이 전부
//   재평가된다 (바인딩 폭풍). 값이 실제로 바뀐 것만 방출해야 한다.
//
// 사용
//   if (assignIfChanged(m_battery, d.batteryVoltage)) emit batteryVoltageChanged();
//
// double 에 epsilon 을 두지 않는 이유
//   센서값 노이즈를 걸러줄 것 같지만, "표시에 의미 없는 변화"의 기준은 필드마다
//   다르다. 임의의 epsilon 으로 실제 변화를 삼키는 쪽이 여분의 emit 보다 나쁘다.
//   연속 변화하는 값이 10Hz 로 emit 되는 건 정상 동작이지 폭풍이 아니다.
//   (폭풍은 "안 바뀐 값까지 전부" 방출할 때 생긴다.)
template <typename T>
inline bool assignIfChanged(T& dst, const T& src)
{
    if (dst == src) return false;
    dst = src;
    return true;
}

// 배열 필드용 (motorPosition[12], legContact[4] 등).
template <typename T>
inline bool assignArrayIfChanged(T* dst, const T* src, int n)
{
    bool changed = false;
    for (int i = 0; i < n; ++i)
    {
        if (dst[i] != src[i]) { dst[i] = src[i]; changed = true; }
    }
    return changed;
}
