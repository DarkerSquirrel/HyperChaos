#pragma once
#include "extern.h"
#include "vmm.h"
#include "vmx.h"

BOOL HvSetupVmcsDefaults(PVMM_PROCESSOR_CONTEXT Context, SIZE_T HostRIP, SIZE_T HostRSP, SIZE_T GuestRIP, SIZE_T GuestRSP);

VMX_ERROR HvSetupVmcsHostArea(PVMM_PROCESSOR_CONTEXT Context, SIZE_T HostRIP, SIZE_T HostRSP);

VMX_ERROR HvSetupVmcsGuestArea(PVMM_PROCESSOR_CONTEXT Context, SIZE_T GuestRIP, SIZE_T GuestRSP);

VMX_ERROR HvSetupVmcsControlFields(PVMM_PROCESSOR_CONTEXT Context);

IA32_VMX_PINBASED_CTLS_REGISTER HvSetupVmcsControlPinBased(PVMM_PROCESSOR_CONTEXT Context);

IA32_VMX_PROCBASED_CTLS_REGISTER HvSetupVmcsControlProcessor(PVMM_PROCESSOR_CONTEXT Context);

IA32_VMX_PROCBASED_CTLS2_REGISTER HvSetupVmcsControlSecondaryProcessor(PVMM_PROCESSOR_CONTEXT Context);

IA32_VMX_ENTRY_CTLS_REGISTER HvSetupVmcsControlVmEntry(PVMM_PROCESSOR_CONTEXT Context);

IA32_VMX_EXIT_CTLS_REGISTER HvSetupVmcsControlVmExit(PVMM_PROCESSOR_CONTEXT Context);