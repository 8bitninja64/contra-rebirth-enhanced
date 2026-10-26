// Per-game constants for Contra ReBirth (WKTE, WiiWare, NTSC-U).
// Addresses come from config/symbols.txt (dtk + library naming pass); r13 offsets
// were read from the instruction that loads the variable in the named SDK function.
#pragma once
#define GAME_NAME        "Contra ReBirth"
#define PORT_NAME        "Contra ReBirth Enhanced XD"   // this port (window title, menu, About)
#define PORT_NAME_UPPER  "CONTRA REBIRTH ENHANCED XD"
#define GAME_EXE         "contrarebirth"
#define GAME_INI         "contrarebirth.ini"

#define G_AX_RESUME_CB          0x8017CE30u   // AX DSP-frame-done callback (fn_8030800C in the Chocobo's Dungeon recomp this was ported from)
#define G_DECREMENTER_CB        0x80155950u   // DecrementerExceptionCallback
#define G_IOS_FREE              0x8018B020u   // iosFree(heap, ptr)
#define G_OS_EXIT_THREAD        0x8015E5D0u   // OSExitThread
#define G_SDA_INTERRUPT_TABLE   0x4F70u       // __OSGetInterruptHandler: lwz r4,-0x4F70(r13)
#define G_SDA_IOS_HEAP          0x54B4u       // __ios_Ipc2: lwz r3,-0x54B4(r13) before iosFree
#define G_FN_OS_EXIT_THREAD     f_8015E5D0
#define G_FN_IOS_FREE           f_8018B020
