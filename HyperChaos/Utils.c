
#include "Utils.h"
#include "PE.h"
#include "CPU.h"
#include "Common.h"
#include "Native.h"
#include <stdarg.h>

BOOL HvUtilBitIsSet(SIZE_T BitField, SIZE_T BitPosition)
{
	return (BitField >> BitPosition) & 1UL;
}

SIZE_T HvUtilBitSetBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField | (1ULL << BitPosition);
}

SIZE_T HvUtilBitClearBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField & ~(1ULL << BitPosition);
}

SIZE_T HvUtilEncodeMustBeBits(SIZE_T DesiredValue, SIZE_T ControlMSR)
{
	LARGE_INTEGER ControlMSRLargeInteger;

	ControlMSRLargeInteger.QuadPart = ControlMSR;

	DesiredValue &= ControlMSRLargeInteger.HighPart;
	DesiredValue |= ControlMSRLargeInteger.LowPart;

	return DesiredValue;
}

VOID HvUtilLog(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[*] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}

VOID HvUtilLogDebug(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[DEBUG] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}

VOID HvUtilLogSuccess(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[+] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}

VOID HvUtilLogError(LPCSTR MessageFormat, ...)
{
	va_list ArgumentList;

	va_start(ArgumentList, MessageFormat);
	vDbgPrintExWithPrefix("[!] ", DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, MessageFormat, ArgumentList);
	va_end(ArgumentList);
}


PVOID g_KernelBase = NULL;
ULONG g_KernelSize = 0;
PSYSTEM_SERVICE_DESCRIPTOR_TABLE g_SSDT = NULL;

CPU_VENDOR UtilCPUVendor()
{
    CPUID data = { 0 };
    char vendor[0x20] = { 0 };
    __cpuid((int*)&data, 0);
    *(int*)(vendor) = data.ebx;
    *(int*)(vendor + 4) = data.edx;
    *(int*)(vendor + 8) = data.ecx;

    if (memcmp(vendor, "GenuineIntel", 12) == 0)
        return CPU_Intel;
    if (memcmp(vendor, "AuthenticAMD", 12) == 0)
        return CPU_AMD;

    return CPU_Other;
}

PVOID UtilKernelBase(OUT PULONG pSize)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytes = 0;
    PRTL_PROCESS_MODULES pMods = NULL;
    PVOID checkPtr = NULL;
    UNICODE_STRING routineName;

    if (g_KernelBase != NULL)
    {
        if (pSize)
            *pSize = g_KernelSize;
        return g_KernelBase;
    }

    RtlInitUnicodeString(&routineName, L"NtOpenFile");

    checkPtr = MmGetSystemRoutineAddress(&routineName);
    if (checkPtr == NULL)
        return NULL;

    __try
    {
        status = ZwQuerySystemInformation(SystemModuleInformation, 0, bytes, &bytes);
        if (bytes == 0)
        {
            DPRINT("BlackBone: %s: Invalid SystemModuleInformation size\n", CPU_IDX, __FUNCTION__);
            return NULL;
        }

        pMods = (PRTL_PROCESS_MODULES)ExAllocatePoolWithTag(NonPagedPoolNx, bytes, HB_POOL_TAG);
        RtlZeroMemory(pMods, bytes);

        status = ZwQuerySystemInformation(SystemModuleInformation, pMods, bytes, &bytes);

        if (NT_SUCCESS(status))
        {
            PRTL_PROCESS_MODULE_INFORMATION pMod = pMods->Modules;

            for (ULONG i = 0; i < pMods->NumberOfModules; i++)
            {
                if (checkPtr >= pMod[i].ImageBase &&
                    checkPtr < (PVOID)((PUCHAR)pMod[i].ImageBase + pMod[i].ImageSize))
                {
                    g_KernelBase = pMod[i].ImageBase;
                    g_KernelSize = pMod[i].ImageSize;
                    if (pSize)
                        *pSize = g_KernelSize;
                    break;
                }
            }
        }

    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT("BlackBone: %s: Exception\n", CPU_IDX, __FUNCTION__);
    }

    if (pMods)
        ExFreePoolWithTag(pMods, HB_POOL_TAG);

    return g_KernelBase;
}

PSYSTEM_SERVICE_DESCRIPTOR_TABLE UtilSSDTBase()
{
    PUCHAR ntosBase = UtilKernelBase(NULL);

    if (g_SSDT != NULL)
        return g_SSDT;

    if (!ntosBase)
        return NULL;

    PIMAGE_NT_HEADERS pHdr = RtlImageNtHeader(ntosBase);
    PIMAGE_SECTION_HEADER pFirstSec = (PIMAGE_SECTION_HEADER)(pHdr + 1);
    for (PIMAGE_SECTION_HEADER pSec = pFirstSec; pSec < pFirstSec + pHdr->FileHeader.NumberOfSections; pSec++)
    {
        if (pSec->Characteristics & IMAGE_SCN_MEM_NOT_PAGED &&
            pSec->Characteristics & IMAGE_SCN_MEM_EXECUTE &&
            !(pSec->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) &&
            (*(PULONG)pSec->Name != 'TINI') &&
            (*(PULONG)pSec->Name != 'EGAP'))
        {
            PVOID pFound = NULL;

            UCHAR pattern[] = "\x4c\x8d\x15\xcc\xcc\xcc\xcc\x4c\x8d\x1d\xcc\xcc\xcc\xcc\xf7";
            NTSTATUS status = UtilSearchPattern(pattern, 0xCC, sizeof(pattern) - 1, ntosBase + pSec->VirtualAddress, pSec->Misc.VirtualSize, &pFound);
            if (NT_SUCCESS(status))
            {
                g_SSDT = (PSYSTEM_SERVICE_DESCRIPTOR_TABLE)((PUCHAR)pFound + *(PULONG)((PUCHAR)pFound + 3) + 7);
                return g_SSDT;
            }
        }
    }

    return NULL;
}

PVOID UtilSSDTEntry(IN ULONG index)
{
    ULONG size = 0;
    PSYSTEM_SERVICE_DESCRIPTOR_TABLE pSSDT = UtilSSDTBase();
    PVOID pBase = UtilKernelBase(&size);

    if (pSSDT && pBase)
    {
        if (index > pSSDT->NumberOfServices)
            return NULL;

        return (PUCHAR)pSSDT->ServiceTableBase + (((PLONG)pSSDT->ServiceTableBase)[index] >> 4);
    }

    return NULL;
}

NTSTATUS UtilQueryPhysicalMemory()
{
    if (g_Data->Memory != NULL)
        return STATUS_SUCCESS;

    PPHYSICAL_MEMORY_RANGE pBaseRange = MmGetPhysicalMemoryRanges();
    if (pBaseRange == NULL)
        return STATUS_UNSUCCESSFUL;

    ULONG runsCount = 0, pageCount = 0;
    for (PPHYSICAL_MEMORY_RANGE pRange = pBaseRange; pRange->NumberOfBytes.QuadPart != 0; pRange++)
    {
        pageCount += (ULONG)PFN(pRange->NumberOfBytes.QuadPart);
        runsCount++;
    }

    IA32_APIC_BASE apic = { 0 };
    apic.All = __readmsr(MSR_APIC_BASE);

    runsCount += 2;
    ULONG size = sizeof(PPHYSICAL_MEMORY_DESCRIPTOR) + runsCount * sizeof(PHYSICAL_MEMORY_RUN);
    g_Data->Memory = ExAllocatePoolWithTag(NonPagedPoolNx, size, HB_POOL_TAG);
    if (g_Data->Memory != NULL)
    {
        RtlZeroMemory(g_Data->Memory, size);

        g_Data->Memory->NumberOfPages = pageCount;
        g_Data->Memory->NumberOfRuns = runsCount;

        runsCount = 0;
        for (PPHYSICAL_MEMORY_RANGE pRange = pBaseRange; pRange->BaseAddress.QuadPart != 0; pRange++, runsCount++)
        {
            g_Data->Memory->Run[runsCount].BasePage = PFN(pRange->BaseAddress.QuadPart);
            g_Data->Memory->Run[runsCount].PageCount = PFN(pRange->NumberOfBytes.QuadPart);
        }

        // APIC and other hardcoded stuff
        g_Data->Memory->Run[runsCount].BasePage = apic.Fields.Apic_base;
        g_Data->Memory->Run[runsCount].PageCount = 1;
        g_Data->Memory->Run[runsCount + 1].BasePage = PFN(0xF0000000);
        g_Data->Memory->Run[runsCount + 1].PageCount = 0x10000;

        ExFreePool(pBaseRange);
        return STATUS_SUCCESS;
    }

    ExFreePool(pBaseRange);
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS UtilProtectNonpagedMemory(IN PVOID ptr, IN ULONG64 size, IN ULONG protection)
{
    NTSTATUS status = STATUS_SUCCESS;
    PMDL pMdl = IoAllocateMdl(ptr, (ULONG)size, FALSE, FALSE, NULL);
    if (pMdl)
    {
        MmBuildMdlForNonPagedPool(pMdl);
        pMdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;
        status = MmProtectMdlSystemAddress(pMdl, protection);
        IoFreeMdl(pMdl);
        return status;
    }

    return STATUS_UNSUCCESSFUL;
}

NTSTATUS UtilSearchPattern(IN PCUCHAR pattern, IN UCHAR wildcard, IN ULONG_PTR len, IN const VOID* base, IN ULONG_PTR size, OUT PVOID* ppFound)
{
    NT_ASSERT(ppFound != NULL && pattern != NULL && base != NULL);
    if (ppFound == NULL || pattern == NULL || base == NULL)
        return STATUS_INVALID_PARAMETER;

    __try
    {
        for (ULONG_PTR i = 0; i < size - len; i++)
        {
            BOOLEAN found = TRUE;
            for (ULONG_PTR j = 0; j < len; j++)
            {
                if (pattern[j] != wildcard && pattern[j] != ((PCUCHAR)base)[i + j])
                {
                    found = FALSE;
                    break;
                }
            }

            if (found != FALSE)
            {
                *ppFound = (PUCHAR)base + i;
                return STATUS_SUCCESS;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return STATUS_UNHANDLED_EXCEPTION;
    }

    return STATUS_NOT_FOUND;
}

NTSTATUS UtilScanSection(IN PCCHAR section, IN PCUCHAR pattern, IN UCHAR wildcard, IN ULONG_PTR len, OUT PVOID* ppFound)
{
    NT_ASSERT(ppFound != NULL);
    if (ppFound == NULL)
        return STATUS_INVALID_PARAMETER;

    PVOID base = UtilKernelBase(NULL);
    if (!base)
        return STATUS_NOT_FOUND;

    PIMAGE_NT_HEADERS64 pHdr = RtlImageNtHeader(base);
    if (!pHdr)
        return STATUS_INVALID_IMAGE_FORMAT;

    PIMAGE_SECTION_HEADER pFirstSection = (PIMAGE_SECTION_HEADER)(pHdr + 1);
    for (PIMAGE_SECTION_HEADER pSection = pFirstSection; pSection < pFirstSection + pHdr->FileHeader.NumberOfSections; pSection++)
    {
        ANSI_STRING s1, s2;
        RtlInitAnsiString(&s1, section);
        RtlInitAnsiString(&s2, (PCCHAR)pSection->Name);
        if (RtlCompareString(&s1, &s2, TRUE) == 0)
            return UtilSearchPattern(pattern, wildcard, len, (PUCHAR)base + pSection->VirtualAddress, pSection->Misc.VirtualSize, ppFound);
    }

    return STATUS_NOT_FOUND;
}