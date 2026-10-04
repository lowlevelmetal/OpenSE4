#!/usr/bin/env python3
"""Checks that Windows programs load on Windows 7 SP1 (64-bit).

    tools/check_windows_imports.py [--msvc-runtime] [--list] FILE.exe|FILE.dll...

Windows refuses to start a program when a DLL it imports is missing or lacks one
of the imported functions; the user sees "The procedure entry point ... could not
be located" or "... .dll is missing" and nothing else. This reads the import
tables (ordinary and delay-loaded) of each PE file given and fails when it
imports:

- a DLL that is not on the list of DLLs a fresh Windows 7 SP1 x64 has
  (WINDOWS7_DLLS): the Universal C Runtime (api-ms-win-crt-*.dll,
  ucrtbase.dll) arrived only with update KB2999226, the newer API sets and
  shcore.dll with Windows 8 or later, and a DLL nobody has reviewed yet fails
  too, so that each new one gets looked at;
- a function that Windows 7 lacks (NEWER_FUNCTIONS): functions of Windows 8
  and later in the system DLLs, and the symbols that msvcrt.dll gained after
  Windows Vista (Windows 7's msvcrt.dll has no symbols beyond Vista's).

A program may still use newer functions through GetProcAddress with a fallback
(SDL3 does for DPI awareness, thread names and precise time): only static
imports are checked. DLLs given on the command line count as present, so an
executable and the SDL3.dll beside it are checked together.

--msvc-runtime allows the Visual C++ runtime (vcruntime140.dll, msvcp140.dll)
and the Universal C Runtime, which MSVC builds link dynamically. Such a build
needs the Visual C++ Redistributable, and on Windows 7 the update KB2999226
before it: the release packages are made with the msvcrt.dll toolchain
(dist-windows) instead, and are checked without the option.

The lists name functions by their documented minimum Windows version
("Minimum supported client" in Microsoft's documentation) and, for msvcrt.dll,
by the Windows version that added them to it as listed in MinGW-w64's
lib-common/msvcrt.def.in. They need not be complete: the toolchain's headers
(_WIN32_WINNT 0x0601) already hide newer functions from our own code. They
catch what a library or a runtime brings in. Add to them when a new
dependency imports something unlisted and Microsoft documents it as Windows 8
or later.

--list prints every imported DLL and function. Before it reads the files, the
check runs itself on two small programs it builds in memory, one of which must
fail, so that a broken reader cannot pass everything. Exit status: 0 when every
file passes, 1 when one does not, 2 for an unreadable file, a failed self-check
or a usage error.

Tested only by import analysis and under Wine, which provides newer functions
whatever Windows version it reports: neither proves that a program runs on
Windows 7, but a missing DLL or function is the usual reason it would not.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# --- what Windows 7 SP1 x64 has --------------------------------------------------

# System DLLs of a fresh Windows 7 SP1 x64 install (System32), lower case: the ones
# Windows programs and their libraries commonly import. Reviewed against
# Microsoft's documentation; add a DLL only after checking that Windows 7 SP1
# ships it without updates or redistributables (xinput1_3.dll and the
# d3dx/d3dcompiler_4x DLLs, for example, come with the DirectX redistributable,
# not with Windows).
WINDOWS7_DLLS = {
    "advapi32.dll", "avrt.dll", "bcrypt.dll", "bcryptprimitives.dll", "cfgmgr32.dll",
    "comctl32.dll", "comdlg32.dll", "crypt32.dll", "d2d1.dll", "d3d10.dll",
    "d3d10_1.dll", "d3d11.dll", "d3d9.dll", "dbghelp.dll", "dinput8.dll", "dnsapi.dll",
    "dsound.dll", "dwmapi.dll", "dwrite.dll", "dxgi.dll", "gdi32.dll", "gdiplus.dll",
    "glu32.dll", "hid.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll",
    "kernelbase.dll", "ksuser.dll", "mf.dll", "mfplat.dll", "mfreadwrite.dll",
    "mpr.dll", "msimg32.dll", "msvcrt.dll", "mswsock.dll", "netapi32.dll",
    "normaliz.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "opengl32.dll",
    "powrprof.dll", "propsys.dll", "psapi.dll", "rpcrt4.dll", "secur32.dll",
    "setupapi.dll", "shell32.dll", "shlwapi.dll", "user32.dll", "userenv.dll",
    "usp10.dll", "uxtheme.dll", "version.dll", "windowscodecs.dll", "winhttp.dll",
    "wininet.dll", "winmm.dll", "winspool.drv", "wldap32.dll", "ws2_32.dll",
    "wsock32.dll", "wtsapi32.dll", "xinput9_1_0.dll",
}

# The Visual C++ runtime, allowed with --msvc-runtime (it needs the Universal C
# Runtime, which Windows 7 gets from update KB2999226).
MSVC_RUNTIME_DLLS = {
    "concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "vcruntime140.dll",
    "vcruntime140_1.dll", "ucrtbase.dll",
}

# Why a DLL that Windows 7 lacks is missing, for the messages.
def missing_dll_reason(name: str) -> str:
    if name.startswith("api-ms-win-crt-") or name == "ucrtbase.dll":
        return ("the Universal C Runtime: Windows 7 has it only after update KB2999226 "
                "(build with the dist-windows preset, which uses msvcrt.dll)")
    if name.startswith(("api-ms-win-", "ext-ms-win-")):
        return "an API set of Windows 8 or later"
    if name in MSVC_RUNTIME_DLLS:
        return "the Visual C++ runtime (the Visual C++ Redistributable, which needs KB2999226 on Windows 7)"
    newer = {
        "combase.dll": "Windows 8", "d3d12.dll": "Windows 10", "dcomp.dll": "Windows 8",
        "dxcore.dll": "Windows 10", "shcore.dll": "Windows 8.1", "xinput1_4.dll": "Windows 8",
        "windows.storage.dll": "Windows 8", "mincore.dll": "Windows 8",
        "xinput1_3.dll": "the DirectX redistributable",
    }
    if name in newer:
        return f"part of {newer[name]}, not of Windows 7"
    return "not on the reviewed list of Windows 7 SP1 DLLs (WINDOWS7_DLLS)"


# Functions of system DLLs that Windows 7 SP1 lacks, by DLL, with the first
# Windows version that has them.
NEWER_FUNCTIONS: dict[str, dict[str, str]] = {}


def _newer(dll: str, version: str, names: str) -> None:
    table = NEWER_FUNCTIONS.setdefault(dll, {})
    for name in names.split():
        table[name] = version


# kernel32.dll (and the same names wherever they are imported from)
_newer("kernel32.dll", "Windows 8", """
    AddDllDirectory RemoveDllDirectory SetDefaultDllDirectories
    ClosePackageInfo CopyFile2 CreateFile2 CreateFileMappingFromApp
    DeleteSynchronizationBarrier EnterSynchronizationBarrier InitializeSynchronizationBarrier
    EnumDynamicTimeZoneInformation GetDynamicTimeZoneInformationEffectiveYears
    GetApplicationUserModelId GetCurrentApplicationUserModelId GetCurrentPackageFamilyName
    GetCurrentPackageFullName GetCurrentPackageId GetCurrentPackageInfo GetCurrentPackagePath
    GetCurrentThreadStackLimits GetFirmwareType GetOverlappedResultEx GetPackageFamilyName
    GetPackageFullName GetPackageId GetPackageInfo GetPackagePath GetPackagesByPackageFamily
    GetProcessInformation GetProcessMitigationPolicy GetSystemTimePreciseAsFileTime
    GetThreadInformation LoadPackagedLibrary MapViewOfFileFromApp OpenPackageInfoByFullName
    PrefetchVirtualMemory SetProcessInformation SetProcessMitigationPolicy SetThreadInformation
    WaitOnAddress WakeByAddressAll WakeByAddressSingle
    GetAppContainerNamedObjectPath CheckTokenCapability GetCachedSigningLevel SetCachedSigningLevel
    RtlAddGrowableFunctionTable RtlDeleteGrowableFunctionTable RtlGrowFunctionTable
""")
_newer("kernel32.dll", "Windows 8.1", """
    DiscardVirtualMemory OfferVirtualMemory ReclaimVirtualMemory
    PssCaptureSnapshot PssFreeSnapshot PssQuerySnapshot PssWalkSnapshot
    QueryProtectedPolicy SetProtectedPolicy
""")
_newer("kernel32.dll", "Windows 10", """
    GetThreadDescription SetThreadDescription
    VirtualAllocFromApp VirtualProtectFromApp OpenFileMappingFromApp
    VirtualAlloc2 VirtualAlloc2FromApp MapViewOfFile3 MapViewOfFile3FromApp MapViewOfFileNuma2
    QueryInterruptTime QueryInterruptTimePrecise QueryUnbiasedInterruptTimePrecise
    GetSystemTimeAdjustmentPrecise SetSystemTimeAdjustmentPrecise
    IsWow64Process2 IsWow64GuestMachineSupported GetSystemWow64Directory2A GetSystemWow64Directory2W
    CreatePseudoConsole ResizePseudoConsole ClosePseudoConsole
    GetSystemCpuSetInformation GetProcessDefaultCpuSets SetProcessDefaultCpuSets
    GetThreadSelectedCpuSets SetThreadSelectedCpuSets GetNumaNodeProcessorMask2
    SetProcessDynamicEHContinuationTargets SetProcessDynamicEnforcedCetCompatibleRanges
    GetOsSafeBootMode GetOsManufacturingMode GetIntegratedDisplaySize
    CreateEnclave InitializeEnclave LoadEnclaveData DeleteEnclave CallEnclave TerminateEnclave
    ConvertAuxiliaryCounterToPerformanceCounter ConvertPerformanceCounterToAuxiliaryCounter
    QueryAuxiliaryCounterFrequency
""")
_newer("kernel32.dll", "Windows 11", """
    GetTempPath2A GetTempPath2W GetMachineTypeAttributes GetFileInformationByName
    CreateDirectory2A CreateDirectory2W RemoveDirectory2A RemoveDirectory2W
    DeleteFile2A DeleteFile2W Wow64SetThreadDefaultGuestMachine
""")
# ntdll.dll
_newer("ntdll.dll", "Windows 8", """
    RtlAddGrowableFunctionTable RtlDeleteGrowableFunctionTable RtlGrowFunctionTable
    RtlWaitOnAddress RtlWakeAddressAll RtlWakeAddressSingle
    NtCreateWaitCompletionPacket NtAssociateWaitCompletionPacket NtCancelWaitCompletionPacket
""")
_newer("ntdll.dll", "Windows 8.1", "NtSetInformationVirtualMemory")
_newer("ntdll.dll", "Windows 10", "RtlGetDeviceFamilyInfoEnum")
# user32.dll: per-monitor DPI, pointer input, and other Windows 8+ functions
_newer("user32.dll", "Windows 8", """
    EnableMouseInPointer IsMouseInPointerEnabled GetPointerType GetPointerCursorId
    GetPointerInfo GetPointerInfoHistory GetPointerFrameInfo GetPointerFrameInfoHistory
    GetPointerTouchInfo GetPointerTouchInfoHistory GetPointerFrameTouchInfo
    GetPointerFrameTouchInfoHistory GetPointerPenInfo GetPointerPenInfoHistory
    GetPointerFramePenInfo GetPointerFramePenInfoHistory SkipPointerFrameMessages
    GetPointerDevices GetPointerDevice GetPointerDeviceProperties GetPointerDeviceRects
    GetPointerDeviceCursors GetRawPointerDeviceData GetPointerInputTransform
    RegisterPointerDeviceNotifications RegisterPointerInputTarget UnregisterPointerInputTarget
    InitializeTouchInjection InjectTouchInput GetCurrentInputMessageSource GetCIMSSM
    GetDisplayAutoRotationPreferences SetDisplayAutoRotationPreferences GetAutoRotationState
    RegisterSuspendResumeNotification UnregisterSuspendResumeNotification IsImmersiveProcess
    GetWindowFeedbackSetting SetWindowFeedbackSetting EvaluateProximityToRect
    EvaluateProximityToPolygon PackTouchHitTestingProximityEvaluation
    RegisterTouchHitTestingWindow GetUnpredictedMessagePos
""")
_newer("user32.dll", "Windows 8.1", """
    LogicalToPhysicalPointForPerMonitorDPI PhysicalToLogicalPointForPerMonitorDPI
""")
_newer("user32.dll", "Windows 10", """
    GetDpiForWindow GetDpiForSystem GetSystemMetricsForDpi AdjustWindowRectExForDpi
    SystemParametersInfoForDpi EnableNonClientDpiScaling SetProcessDpiAwarenessContext
    SetThreadDpiAwarenessContext GetThreadDpiAwarenessContext GetWindowDpiAwarenessContext
    GetAwarenessFromDpiAwarenessContext AreDpiAwarenessContextsEqual IsValidDpiAwarenessContext
    GetDpiFromDpiAwarenessContext SetThreadDpiHostingBehavior GetThreadDpiHostingBehavior
    GetWindowDpiHostingBehavior GetDpiAwarenessContextForProcess GetSystemDpiForProcess
    SetDialogDpiChangeBehavior GetDialogDpiChangeBehavior SetDialogControlDpiChangeBehavior
    GetDialogControlDpiChangeBehavior CreateSyntheticPointerDevice InjectSyntheticPointerInput
    DestroySyntheticPointerDevice IsWindowArranged
""")
# gdi32.dll
_newer("gdi32.dll", "Windows 8", "D3DKMTOpenAdapterFromLuid")
_newer("gdi32.dll", "Windows 10", "D3DKMTEnumAdapters2 D3DKMTQueryVideoMemoryInfo")
# advapi32.dll
_newer("advapi32.dll", "Windows 8", """
    EventSetInformation AddResourceAttributeAce AddScopedPolicyIDAce TraceQueryInformation
""")
# shell32.dll
_newer("shell32.dll", "Windows 8", "SHAssocEnumHandlersForProtocolByApplication")
# ole32.dll
_newer("ole32.dll", "Windows 8", """
    CoIncrementMTAUsage CoDecrementMTAUsage CoWaitForMultipleObjects CoCreateInstanceFromApp
""")
_newer("ole32.dll", "Windows 10", "CoRegisterActivationFilter")
# ws2_32.dll
_newer("ws2_32.dll", "Windows 8", "GetAddrInfoExCancel GetAddrInfoExOverlappedResult GetHostNameW")
_newer("ws2_32.dll", "Windows 10", "ProcessSocketNotifications WSAGetIPUserMtu WSASetIPUserMtu")
# iphlpapi.dll
_newer("iphlpapi.dll", "Windows 8", "GetIpNetworkConnectionBandwidthEstimates")
_newer("iphlpapi.dll", "Windows 10", """
    GetIfEntry2Ex NotifyNetworkConnectivityHintChange GetNetworkConnectivityHint
    GetNetworkConnectivityHintForInterface GetInterfaceActiveTimestampCapabilities
    GetInterfaceSupportedTimestampCapabilities RegisterInterfaceTimestampConfigChange
    UnregisterInterfaceTimestampConfigChange
""")
# bcrypt.dll and bcryptprimitives.dll
_newer("bcrypt.dll", "Windows 8", "BCryptKeyDerivation")
_newer("bcrypt.dll", "Windows 8.1", "BCryptCreateMultiHash BCryptProcessMultiOperations")
_newer("bcrypt.dll", "Windows 10", "BCryptHash")
_newer("bcryptprimitives.dll", "Windows 8", "ProcessPrng")
# cfgmgr32.dll and setupapi.dll
_newer("cfgmgr32.dll", "Windows 8", "CM_Register_Notification CM_Unregister_Notification CM_MapCrToWin32Err")
# dwmapi.dll
_newer("dwmapi.dll", "Windows 8", "DwmRenderGesture DwmTetherContact DwmShowContact")
# imm32.dll
_newer("imm32.dll", "Windows 8", "ImmDisableLegacyIME")
# dxgi.dll and d3d11.dll
_newer("dxgi.dll", "Windows 8.1", "CreateDXGIFactory2 DXGIGetDebugInterface1")
_newer("dxgi.dll", "Windows 10", "DXGIDeclareAdapterRemovalSupport")
_newer("d3d11.dll", "Windows 8", "CreateDirect3D11DeviceFromDXGIDevice CreateDirect3D11SurfaceFromDXGISurface")
_newer("d3d11.dll", "Windows 10", "D3D11On12CreateDevice")
# msvcrt.dll: symbols added after Windows Vista (x64), from MinGW-w64's
# lib-common/msvcrt.def.in. Windows 7 added none.
_newer("msvcrt.dll", "Windows 8", "_wcstod_l")
_newer("msvcrt.dll", "Windows 8.1", """
    __ExceptionPtrAssign __ExceptionPtrCompare __ExceptionPtrCopy __ExceptionPtrCopyException
    __ExceptionPtrCreate __ExceptionPtrCurrentException __ExceptionPtrDestroy
    __ExceptionPtrRethrow __ExceptionPtrSwap __ExceptionPtrToBool
    _W_Getdays _W_Getmonths _W_Gettnames _Wcsftime __AdjustPointer
""")
_newer("msvcrt.dll", "Windows 11", """
    ?_Doraise@bad_cast@@MEBAXXZ ?_Doraise@bad_typeid@@MEBAXXZ ?_inconsistency@@YAXXZ
    ?_is_exception_typeof@@YAHAEBVtype_info@@PEAU_EXCEPTION_POINTERS@@@Z
    ?name@type_info@@QEBAPEBDPEAU__type_info_node@@@Z
    __BuildCatchObject __BuildCatchObjectHelper __CxxFrameHandler4 __TypeMatch _freefls
    _get_terminate _get_unexpected _ungetc_nolock
""")

# The Windows 8+ functions of kernel32.dll are the same wherever a program
# imports them from (kernelbase.dll, the api-ms-win-core sets).
_KERNEL_NEWER = dict(NEWER_FUNCTIONS["kernel32.dll"])


def newer_version(dll: str, function: str) -> str | None:
    table = NEWER_FUNCTIONS.get(dll)
    if table and function in table:
        return table[function]
    if dll in ("kernel32.dll", "kernelbase.dll") or dll.startswith("api-ms-win-core-"):
        return _KERNEL_NEWER.get(function)
    return None


# --- reading the import tables of a PE file -----------------------------------------

class PEError(Exception):
    pass


class PEFile:
    def __init__(self, data: bytes):
        self.data = data
        d = self.data
        if len(d) < 0x40 or d[:2] != b"MZ":
            raise PEError("not a PE file (no MZ header)")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise PEError("not a PE file (no PE signature)")
        self.machine, sections, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", d, pe + 4)
        opt = pe + 24
        magic = struct.unpack_from("<H", d, opt)[0]
        if magic == 0x20B:  # PE32+
            self.ptr_size = 8
            dirs = opt + 112
            count = struct.unpack_from("<I", d, opt + 108)[0]
        elif magic == 0x10B:  # PE32
            self.ptr_size = 4
            dirs = opt + 96
            count = struct.unpack_from("<I", d, opt + 92)[0]
        else:
            raise PEError(f"unknown optional header magic {magic:#x}")
        self.subsystem_version = struct.unpack_from("<HH", d, opt + 48)
        self.directories = [struct.unpack_from("<II", d, dirs + 8 * i) for i in range(min(count, 16))]
        self.sections = []
        table = opt + opt_size
        for i in range(sections):
            _, vsize, va, rawsize, rawptr = struct.unpack_from("<8sIIII", d, table + 40 * i)
            self.sections.append((va, max(vsize, rawsize), rawptr, rawsize))

    def offset(self, rva: int) -> int:
        for va, size, rawptr, rawsize in self.sections:
            if va <= rva < va + size:
                if rva - va >= rawsize:
                    raise PEError(f"RVA {rva:#x} is not backed by the file")
                return rawptr + rva - va
        raise PEError(f"RVA {rva:#x} is outside every section")

    def cstring(self, rva: int) -> str:
        start = self.offset(rva)
        end = self.data.index(b"\0", start)
        return self.data[start:end].decode("ascii", errors="replace")

    def thunks(self, rva: int) -> list[str]:
        names = []
        pos = self.offset(rva)
        fmt, flag = ("<Q", 1 << 63) if self.ptr_size == 8 else ("<I", 1 << 31)
        while True:
            value = struct.unpack_from(fmt, self.data, pos)[0]
            if value == 0:
                return names
            if value & flag:
                names.append(f"#{value & 0xFFFF}")
            else:
                names.append(self.cstring((value & 0x7FFFFFFF) + 2))
            pos += self.ptr_size

    def imports(self) -> list[tuple[str, bool, list[str]]]:
        """(DLL, delay-loaded, functions) for each imported DLL."""
        result = []
        if len(self.directories) > 1 and self.directories[1][0]:
            pos = self.offset(self.directories[1][0])
            while True:
                lookup, _, _, name, iat = struct.unpack_from("<IIIII", self.data, pos)
                if name == 0:
                    break
                result.append((self.cstring(name), False, self.thunks(lookup or iat)))
                pos += 20
        if len(self.directories) > 13 and self.directories[13][0]:
            pos = self.offset(self.directories[13][0])
            while True:
                attrs, name, _, _, int_rva = struct.unpack_from("<IIIII", self.data, pos)[:5]
                if name == 0:
                    break
                result.append((self.cstring(name), True, self.thunks(int_rva) if int_rva else []))
                pos += 32
        return result


# --- the check -------------------------------------------------------------------

def check(pe: PEFile, shipped: set[str], msvc_runtime: bool, listing: bool) -> list[str]:
    problems = []
    if pe.machine != 0x8664:
        problems.append(f"machine type {pe.machine:#x} is not x86-64")
    major, minor = pe.subsystem_version
    if (major, minor) > (6, 1):
        problems.append(f"subsystem version {major}.{minor}: Windows 7 (6.1) refuses to start it")
    for dll, delayed, functions in pe.imports():
        key = dll.lower()
        if listing:
            print(f"  {dll}{' (delay-loaded)' if delayed else ''}: {' '.join(sorted(functions))}")
        if key in shipped:
            continue
        allowed = key in WINDOWS7_DLLS or (msvc_runtime and (
            key in MSVC_RUNTIME_DLLS or key.startswith("api-ms-win-crt-")))
        if not allowed:
            what = "delay-loads" if delayed else "imports"
            problems.append(f"{what} {dll}: {missing_dll_reason(key)}")
            continue
        for function in functions:
            version = newer_version(key, function)
            if version:
                problems.append(f"imports {dll}!{function}, which needs {version}")
    return problems


def make_pe(imports: dict[str, list[str]], delayed: dict[str, list[str]] | None = None) -> bytes:
    """A minimal x86-64 PE image with these imports, for the self-check."""
    delayed = delayed or {}
    va, raw = 0x1000, 0x200
    entries = [(dll, functions, False) for dll, functions in imports.items()] + \
              [(dll, functions, True) for dll, functions in delayed.items()]
    # Layout of the one section: descriptors, name tables, hint/name entries, DLL names.
    plain = [e for e in entries if not e[2]]
    late = [e for e in entries if e[2]]
    pos = 20 * (len(plain) + 1) + 32 * (len(late) + 1)
    tables = []
    for _, functions, _ in entries:
        tables.append(pos)
        pos += 8 * (len(functions) + 1)
    hints = []
    for _, functions, _ in entries:
        offsets = []
        for f in functions:
            offsets.append(pos)
            pos += 2 + len(f) + 1 + (len(f) + 1) % 2
        hints.append(offsets)
    names = []
    for dll, _, _ in entries:
        names.append(pos)
        pos += len(dll) + 1
    section = bytearray(pos + (-pos) % 0x200)
    descriptor, late_descriptor = 0, 20 * (len(plain) + 1)
    for i, (dll, functions, is_late) in enumerate(entries):
        if is_late:
            struct.pack_into("<IIIIIIII", section, late_descriptor, 1, va + names[i], 0, va + tables[i],
                             va + tables[i], 0, 0, 0)
            late_descriptor += 32
        else:
            struct.pack_into("<IIIII", section, descriptor, va + tables[i], 0, 0, va + names[i], va + tables[i])
            descriptor += 20
        for j, f in enumerate(functions):
            struct.pack_into("<Q", section, tables[i] + 8 * j, va + hints[i][j])
            section[hints[i][j] + 2:hints[i][j] + 2 + len(f)] = f.encode()
        section[names[i]:names[i] + len(dll)] = dll.encode()
    header = bytearray(raw)
    header[0:2] = b"MZ"
    struct.pack_into("<I", header, 0x3C, 0x40)
    header[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", header, 0x44, 0x8664, 1, 0, 0, 0, 240, 0x22)
    opt = 0x58
    struct.pack_into("<H", header, opt, 0x20B)
    struct.pack_into("<HH", header, opt + 48, 6, 0)
    struct.pack_into("<I", header, opt + 108, 16)
    struct.pack_into("<II", header, opt + 112 + 8, va, 20 * (len(plain) + 1))
    if late:
        struct.pack_into("<II", header, opt + 112 + 13 * 8, va + 20 * (len(plain) + 1), 32 * (len(late) + 1))
    struct.pack_into("<8sIIII", header, opt + 240, b".idata", len(section), va, len(section), raw)
    return bytes(header + section)


def self_check() -> bool:
    good = make_pe({"KERNEL32.dll": ["CreateFileW", "GetSystemTimeAsFileTime"], "msvcrt.dll": ["malloc"]},
                   {"USER32.dll": ["MessageBoxW"]})
    bad = make_pe({"KERNEL32.dll": ["CreateFileW", "GetSystemTimePreciseAsFileTime"],
                   "api-ms-win-crt-runtime-l1-1-0.dll": ["_initterm"]},
                  {"user32.dll": ["GetDpiForWindow"], "shcore.dll": ["SetProcessDpiAwareness"]})
    found = check(PEFile(bad), set(), False, False)
    expected = ["GetSystemTimePreciseAsFileTime", "api-ms-win-crt-runtime", "GetDpiForWindow", "shcore.dll"]
    return (check(PEFile(good), set(), False, False) == [] and len(found) == len(expected)
            and all(any(e in p for p in found) for e in expected))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check that Windows programs import only what Windows 7 SP1 x64 has.")
    parser.add_argument("files", nargs="+", type=Path, help="executables and DLLs to check")
    parser.add_argument("--msvc-runtime", action="store_true",
                        help="allow the Visual C++ and Universal C runtimes (MSVC builds)")
    parser.add_argument("--list", action="store_true", help="print every import")
    args = parser.parse_args()

    if not self_check():
        print("check_windows_imports.py: its self-check failed: fix the PE reader first", file=sys.stderr)
        return 2
    shipped = {f.name.lower() for f in args.files if f.suffix.lower() == ".dll"}
    failed = False
    for path in args.files:
        if args.list:
            print(f"{path}:")
        try:
            problems = check(PEFile(path.read_bytes()), shipped, args.msvc_runtime, args.list)
        except (OSError, PEError, struct.error, ValueError) as e:
            print(f"{path}: cannot read: {e}", file=sys.stderr)
            return 2
        if problems:
            failed = True
            print(f"{path}: will not start on Windows 7 SP1:")
            for p in problems:
                print(f"  {p}")
        else:
            print(f"{path}: imports only what Windows 7 SP1 has")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
