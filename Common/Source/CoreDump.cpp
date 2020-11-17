/*
 * Copyright (c) 2020 Andreas Pohl
 * Licensed under MIT (https://github.com/apohl79/audiogridder/blob/master/COPYING)
 *
 * Author: Andreas Pohl
 */

#include "CoreDump.hpp"
#include "Utils.hpp"
#include "Defaults.hpp"

#ifdef JUCE_WINDOWS
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#endif

#ifdef JUCE_MAC
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include <atomic>

namespace e47 {
namespace CoreDump {

setLogTagStatic("coredump");

std::atomic_bool l_initialized{false};

#ifdef JUCE_WINDOWS

constexpr size_t MAX_NAME = 128;
constexpr size_t MAX_VERSION = 32;

WCHAR l_appName[MAX_NAME];
WCHAR l_path[MAX_PATH];
bool l_showMessage;

LONG dump(EXCEPTION_POINTERS* pExceptionPointers) {
    traceScope();

    // create dump
    HANDLE hDumpFile =
        CreateFileW(l_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    MINIDUMP_EXCEPTION_INFORMATION exceptInfo;
    exceptInfo.ThreadId = GetCurrentThreadId();
    exceptInfo.ExceptionPointers = pExceptionPointers;
    exceptInfo.ClientPointers = TRUE;
    BOOL success = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory,
                                     &exceptInfo, NULL, NULL);

    if (success && l_showMessage) {
        WCHAR info[MAX_PATH + 128];
        StringCchPrintfW(
            info, MAX_PATH + 128,
            L"AudioGridder %s crashed! A minidump has been created at '%s'. Please submit a bug report including this "
            L"file togther with the logfiles! Thanks!",
            l_appName, l_path);
        MessageBoxW(NULL, info, l_appName, MB_OK | MB_ICONERROR);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

void initialize(const String& appName, const String& filePrefix, bool showMessage) {
    traceScope();

    if (!l_initialized.exchange(true)) {
#ifdef JUCE_WINDOWS
        auto file = File(Defaults::getLogFileName(appName, filePrefix, ".dmp")).getNonexistentSibling();
        // create dir if needed
        auto d = file.getParentDirectory();
        if (!d.exists()) {
            d.createDirectory();
        }
        cleanDirectory(d.getFullPathName(), filePrefix, ".dmp", 3);

        logln("core files will be written to " << d.getFullPathName());

        StringCchCopyW(l_appName, MAX_NAME, appName.toWideCharPointer());
        StringCchCopyW(l_path, MAX_PATH, file.getFullPathName().toWideCharPointer());
        l_showMessage = showMessage;
        SetUnhandledExceptionFilter(dump);
#else
        ignoreUnused(appName);
        ignoreUnused(filePrefix);
        ignoreUnused(showMessage);

        struct rlimit limit;
        limit.rlim_cur = RLIM_INFINITY;
        limit.rlim_max = RLIM_INFINITY;
        if (setrlimit(RLIMIT_CORE, &limit) != 0) {
            logln("unable to enable core dumps: setlimit failed: " << strerror(errno));
            return;
        }
#endif

#ifdef JUCE_MAC
        // Check if core files can be written
        char* p;
        size_t len;
        sysctlbyname("kern.corefile", nullptr, &len, nullptr, 0);
        p = (char*)malloc(len);
        sysctlbyname("kern.corefile", p, &len, nullptr, 0);
        String coredirname(p);
        logln("core files will be written to " << coredirname);
        if (File::isAbsolutePath(coredirname)) {
            File coredir = File::createFileWithoutCheckingPath(coredirname).getParentDirectory();
            if (!coredir.isDirectory() || !coredir.hasWriteAccess()) {
                logln("missing write permission to core directory " << coredir.getFullPathName());
                logln("you should run: sudo chmod o+w " << coredir.getFullPathName());
            }
        }
#endif
    }
}

}  // namespace CoreDump
}  // namespace e47
