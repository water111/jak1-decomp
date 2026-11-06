#line 1 "../include/syscall.h"
#line 1 "klib.s"
#line 1 "version.h"
    .data
    .globl __ps2_klibinfo__
__ps2_klibinfo__:
    .ascii "PsIIlibkernl2210"
#line 1 "klib.s"
    .set noreorder
    .text
    .align 6
    .macro syscall_entry name, number
    .globl \name
    .ent \name
\name:
    li $3, \number
    syscall
    jr $31
    nop
    .end \name
    .endm

#line 33 "klib.s"
    syscall_entry RFU000_FullReset, 0
    syscall_entry ResetEE, 1
    syscall_entry SetGsCrt, 2
    syscall_entry RFU003, 3
    syscall_entry Exit, 4
    syscall_entry RFU005, 5
    syscall_entry LoadExecPS2, 6
    syscall_entry ExecPS2, 7
    syscall_entry RFU008, 8
    syscall_entry RFU009, 9
    syscall_entry AddSbusIntcHandler, 10
    syscall_entry RemoveSbusIntcHandler, 11
    syscall_entry Interrupt2Iop, 12
    syscall_entry SetVTLBRefillHandler, 13
    syscall_entry SetVCommonHandler, 14
    syscall_entry SetVInterruptHandler, 15
#line 51 "klib.s"
    syscall_entry AddIntcHandler, 16
    syscall_entry AddIntcHandler2, 16
    syscall_entry RemoveIntcHandler, 17
    syscall_entry AddDmacHandler, 18
    syscall_entry AddDmacHandler2, 18
    syscall_entry RemoveDmacHandler, 19
    syscall_entry _EnableIntc, 20
    syscall_entry _DisableIntc, 21
    syscall_entry _EnableDmac, 22
    syscall_entry _DisableDmac, 23
    syscall_entry SetAlarm, 24
    syscall_entry ReleaseAlarm, 25
    syscall_entry _iEnableIntc, -26
    syscall_entry _iDisableIntc, -27
    syscall_entry _iEnableDmac, -28
    syscall_entry _iDisableDmac, -29
    syscall_entry iSetAlarm, -30
    syscall_entry iReleaseAlarm, -31
#line 71 "klib.s"
    syscall_entry CreateThread, 32
    syscall_entry DeleteThread, 33
    syscall_entry StartThread, 34
    syscall_entry ExitThread, 35
    syscall_entry ExitDeleteThread, 36
    syscall_entry TerminateThread, 37
    syscall_entry iTerminateThread, -38
    syscall_entry DisableDispatchThread, 39
    syscall_entry EnableDispatchThread, 40
    syscall_entry ChangeThreadPriority, 41
    syscall_entry iChangeThreadPriority, -42
    syscall_entry RotateThreadReadyQueue, 43
    syscall_entry _iRotateThreadReadyQueue, -44
    syscall_entry ReleaseWaitThread, 45
    syscall_entry iReleaseWaitThread, -46
    syscall_entry GetThreadId, 47
#line 89 "klib.s"
    syscall_entry ReferThreadStatus, 48
    syscall_entry iReferThreadStatus, -49
    syscall_entry SleepThread, 50
    syscall_entry WakeupThread, 51
    syscall_entry _iWakeupThread, -52
    syscall_entry CancelWakeupThread, 53
    syscall_entry iCancelWakeupThread, -54
    syscall_entry SuspendThread, 55
    syscall_entry _iSuspendThread, -56
    syscall_entry ResumeThread, 57
    syscall_entry iResumeThread, -58
    syscall_entry JoinThread, 59
    syscall_entry RFU060, 60
    syscall_entry RFU061, 61
    syscall_entry EndOfHeap, 62
    syscall_entry RFU063, 63
#line 107 "klib.s"
    syscall_entry CreateSema, 64
    syscall_entry DeleteSema, 65
    syscall_entry SignalSema, 66
    syscall_entry iSignalSema, -67
    syscall_entry WaitSema, 68
    syscall_entry PollSema, 69
    syscall_entry iPollSema, -70
    syscall_entry ReferSemaStatus, 71
    syscall_entry iReferSemaStatus, -72
    syscall_entry RFU073, 73
    syscall_entry SetOsdConfigParam, 74
    syscall_entry GetOsdConfigParam, 75
    syscall_entry GetGsHParam, 76
    syscall_entry GetGsVParam, 77
    syscall_entry SetGsHParam, 78
    syscall_entry SetGsVParam, 79
#line 126 "klib.s"
    syscall_entry RFU080_CreateEventFlag, 80
    syscall_entry RFU081_DeleteEventFlag, 81
    syscall_entry RFU082_SetEventFlag, 82
    syscall_entry RFU083_iSetEventFlag, -83
    syscall_entry RFU084_ClearEventFlag, 84
    syscall_entry RFU085_iClearEventFlag, -85
    syscall_entry RFU086_WaitEvnetFlag, 86
    syscall_entry RFU087_PollEvnetFlag, 87
    syscall_entry RFU088_iPollEvnetFlag, -88
    syscall_entry RFU089_ReferEventFlagStatus, 89
    syscall_entry RFU090_iReferEventFlagStatus, -90
    syscall_entry RFU091, 91
    syscall_entry EnableIntcHandler, 92
    syscall_entry iEnableIntcHandler, -92
    syscall_entry DisableIntcHandler, 93
    syscall_entry iDisableIntcHandler, -93
    syscall_entry EnableDmacHandler, 94
    syscall_entry iEnableDmacHandler, -94
    syscall_entry DisableDmacHandler, 95
    syscall_entry iDisableDmacHandler, -95
#line 148 "klib.s"
    syscall_entry KSeg0, 96
    syscall_entry EnableCache, 97
    syscall_entry DisableCache, 98
    syscall_entry GetCop0, 99
    syscall_entry FlushCache, 100
#line 154 "klib.s"
    syscall_entry CpuConfig, 102
    syscall_entry iGetCop0, -103
    syscall_entry iFlushCache, -104
#line 158 "klib.s"
    syscall_entry iCpuConfig, -106
    syscall_entry sceSifStopDma, 107
    syscall_entry SetCPUTimerHandler, 108
    syscall_entry SetCPUTimer, 109
    syscall_entry SetOsdConfigParam2, 110
    syscall_entry GetOsdConfigParam2, 111
#line 166 "klib.s"
    syscall_entry GsGetIMR, 112
    syscall_entry iGsGetIMR, -112
    syscall_entry GsPutIMR, 113
    syscall_entry iGsPutIMR, -113
    syscall_entry SetPgifHandler, 114
    syscall_entry SetVSyncFlag, 115
    syscall_entry RFU116, 116
    syscall_entry _print, 117
    syscall_entry sceSifDmaStat, 118
    syscall_entry isceSifDmaStat, -118
    syscall_entry sceSifSetDma, 119
    syscall_entry isceSifSetDma, -119
    syscall_entry sceSifSetDChain, 120
    syscall_entry isceSifSetDChain, -120
    syscall_entry sceSifSetReg, 121
    syscall_entry sceSifGetReg, 122
    syscall_entry ExecOSD, 123
    syscall_entry Deci2Call, 124
    syscall_entry PSMode, 125
    syscall_entry MachineType, 126
    syscall_entry GetMemorySize, 127

