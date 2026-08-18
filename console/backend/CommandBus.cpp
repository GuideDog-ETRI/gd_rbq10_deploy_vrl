#include "CommandBus.h"

#include "SharedMemory.h"
#include <common/ENumClasses.hpp>
#include <common/Log.hpp>

extern pCONSOLE_SHM sharedConsole;

void CommandBus::send(int command)
{
    if (!sharedConsole) return;

    COMMAND_STRUCT cmd;
    cmd.COMMAND_TARGET = CMD_TARGET_CONTROLLER;
    cmd.USER_COMMAND = command;

    // 원본과 동일하게 "직전 명령을 덮어쓰는" 단일 슬롯이다. 큐가 아니라서,
    // 10Hz 폴링 한 주기 안에 두 번 누르면 앞의 것이 사라진다. 사람이 누르는
    // 버튼이라 실질적인 문제는 없지만, 자동화가 붙으면 여기가 병목이 된다.
    sharedConsole->COMMAND = cmd;
    sharedConsole->NEWCOMMAND = true;
}
