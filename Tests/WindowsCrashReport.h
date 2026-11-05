#pragma once

// WindowsCrashReport.h: on Windows, prints a symbolized stack to stderr when the Tests binary raises a fatal
// exception (access violation, illegal instruction, stack overflow, integer divide by zero), then lets gtest report
// the failure as usual. Once per test, so a burst of first-chance exceptions cannot flood the log.
#if defined(_WIN32)

#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h must come after windows.h.
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace synth::tests::crashreport {

inline bool isFatalCode(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
           code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO;
}

// "module!symbol+0xoff" when DbgHelp resolves the address, else "module+0xRVA", else the bare address.
inline std::string describeAddress(HANDLE process, DWORD64 address) {
    char buf[512];
    std::string module = "?";
    DWORD64 base = 0;
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &mod)) {
        base = reinterpret_cast<DWORD64>(mod);
        wchar_t path[MAX_PATH] = {};
        if (GetModuleFileNameW(mod, path, MAX_PATH) > 0) {
            const wchar_t* name = wcsrchr(path, L'\\');
            name = name != nullptr ? name + 1 : path;
            char narrow[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
            module = narrow;
        }
    }

    alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, address, &displacement, sym)) {
        std::snprintf(buf, sizeof(buf), "%s!%s+0x%llx", module.c_str(), sym->Name,
                      static_cast<unsigned long long>(displacement));
    } else if (base != 0) {
        std::snprintf(buf, sizeof(buf), "%s+0x%llx", module.c_str(), static_cast<unsigned long long>(address - base));
    } else {
        std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(address));
    }
    return buf;
}

inline void printStack(CONTEXT context) {
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    STACKFRAME64 frame = {};
#if defined(_M_X64)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
    const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0)
            break;
        std::fprintf(stderr, "  #%02d %s\n", i, describeAddress(process, frame.AddrPC.Offset).c_str());
    }
}

inline LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD& rec = *info->ExceptionRecord;
    if (!isFatalCode(rec.ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;

    static const ::testing::TestInfo* lastReported = nullptr;
    static bool reportedOutsideTest = false;
    const auto* test = ::testing::UnitTest::GetInstance()->current_test_info();
    if (test != nullptr ? test == lastReported : reportedOutsideTest)
        return EXCEPTION_CONTINUE_SEARCH;
    if (test != nullptr)
        lastReported = test;
    else
        reportedOutsideTest = true;

    std::fprintf(stderr, "\n=== Windows crash report ===\n");
    std::fprintf(stderr, "code: 0x%08lx\n", static_cast<unsigned long>(rec.ExceptionCode));
    std::fprintf(stderr, "faulting address: 0x%llx\n",
                 static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(rec.ExceptionAddress)));
    if (rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec.NumberParameters >= 2) {
        const ULONG_PTR kind = rec.ExceptionInformation[0];
        std::fprintf(stderr, "access: %s at 0x%llx\n",
                     kind == 0   ? "read"
                     : kind == 1 ? "write"
                                 : "execute (DEP)",
                     static_cast<unsigned long long>(rec.ExceptionInformation[1]));
    }
    if (test != nullptr)
        std::fprintf(stderr, "test: %s.%s\n", test->test_suite_name(), test->name());
    else
        std::fprintf(stderr, "test: (none running)\n");
    std::fprintf(stderr, "stack:\n");
    printStack(*info->ContextRecord);
    std::fprintf(stderr, "=== end crash report ===\n");
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Call once, early in main().
inline void install() {
    DWORD options = SymGetOptions();
    SymSetOptions(options | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (wchar_t* slash = wcsrchr(exePath, L'\\'))
        *slash = L'\0';
    SymInitializeW(GetCurrentProcess(), exePath, TRUE);
    AddVectoredExceptionHandler(1, onException);
}

} // namespace synth::tests::crashreport

#endif // _WIN32
