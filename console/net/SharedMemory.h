#ifndef SHAREDMEMORY_H
#define SHAREDMEMORY_H
#include "common/SharedMemory.hpp"

#define ManiControlAL 4

typedef struct _CONSOLE_SHM_
{
    bool bIsConnect;
    int LanComm_Mode; // TODO: mode???? connection PC; robot(3), local(1) ... etc
    int mc_ch; // TODO: last selected???

    TELEMETRY_FRAME telemetry;

    COMMAND_STRUCT  COMMAND;
    bool NEWCOMMAND;

}CONSOLE_SHM, *pCONSOLE_SHM;
extern pCONSOLE_SHM sharedConsole;

#endif // SHAREDMEMORY_H
