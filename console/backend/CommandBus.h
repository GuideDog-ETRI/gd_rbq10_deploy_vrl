#pragma once

#include <QObject>

#include <common/ENumClasses.hpp>

// QML → 로봇 명령 송신.
//
// 원본은 버튼 슬롯 하나마다 아래 4줄을 반복했다 (19개 버튼 × 4줄):
//     COMMAND_STRUCT cmd;
//     cmd.USER_COMMAND = CMD_CTRL_STAND;
//     cmd.COMMAND_TARGET = CMD_TARGET_CONTROLLER;
//     sharedConsole->COMMAND = cmd;  sharedConsole->NEWCOMMAND = true;
// 전부 같은 모양이라 QML 에서 Cmd.send(Cmd.Stand) 한 줄로 줄인다.
//
// 실제 전송은 StateBridge 의 10Hz 폴링이 NEWCOMMAND 를 보고 처리한다 (원본
// MainWindow::tcpSend 와 동일). 여기서 직접 소켓을 건드리지 않는 이유는,
// "명령을 기록하는 것"과 "보내는 것"을 분리해 둬야 연결이 끊긴 동안 눌린
// 명령의 처리(현재는 버림)를 한 곳에서 바꿀 수 있기 때문이다.
class CommandBus : public QObject
{
    Q_OBJECT

public:
    // QML 에서 Cmd.Stand 처럼 쓰기 위해 Q_ENUM 으로 노출한다.
    //
    // ⚠️ 값은 _COMMAND_SET_ 에서 **직접 받아온다.** 여기 적힌 숫자가 그대로
    // USER_COMMAND 로 전선에 나가기 때문이다 (CommandBus::send 는 변환하지 않는다).
    // 예전에는 0 부터 세는 별도 번호였고, 그래서 화면 버튼이 START→0 / STAND→2 처럼
    // 로봇이 모르는 값을 보냈다 — 로봇은 조용히 무시한다.
    //
    // 이 다섯이 콘솔이 보내는 명령의 전부다. 상류에서 딸려온 미배선 명령 19개
    // (MPC/VISION/STAIR/HARNESS/SAVE/…)는 2026-08-15 전수 조사 후 enum 째 지웠다.
    enum Command {
        Start = CMD_CTRL_START,  // "ROBOT START"
        EStop = CMD_CTRL_E_STOP, // "Emergency"
        Stand = CMD_CTRL_STAND,
        Walk  = CMD_CTRL_WALK,
        Ready = CMD_CTRL_READY,  // 화면 라벨은 "SIT"
    };
    Q_ENUM(Command)

    explicit CommandBus(QObject* parent = nullptr) : QObject(parent) {}

    // 컨트롤러(CMD_TARGET_CONTROLLER) 로 보낸다. 대상이 하나뿐이라 target 인자는 없다.
    Q_INVOKABLE void send(int command);
};
