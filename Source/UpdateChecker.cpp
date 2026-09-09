#include "UpdateChecker.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <string>
#endif

// Find the VST3 bundle folder that holds the module we're running from.
// A JUCE VST3 on Windows lives at:
//   <bundle>/MPE Bender.vst3/Contents/x86_64-win/MPE Bender.vst3   (the actual DLL)
// so the bundle root is three parents up from the loaded module file.
juce::File UpdateChecker::installedBundle()
{
#if JUCE_WINDOWS
    static const int moduleAnchor = 0;   // any address inside this module
    HMODULE hm = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                               | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&moduleAnchor), &hm)
        && hm != nullptr)
    {
        wchar_t buffer[MAX_PATH * 2] = {};
        if (GetModuleFileNameW(hm, buffer, (DWORD) juce::numElementsInArray(buffer)) > 0)
        {
            const juce::File module { juce::String(buffer) };
            // .../Name.vst3/Contents/x86_64-win/Name.vst3  ->  .../Name.vst3
            auto bundle = module.getParentDirectory()      // x86_64-win
                                .getParentDirectory()      // Contents
                                .getParentDirectory();     // Name.vst3
            if (bundle.getFileName().endsWithIgnoreCase(".vst3"))
                return bundle;
            // Standalone / unexpected layout: nothing safe to replace.
        }
    }
    return {};
#else
    return {};
#endif
}

bool UpdateChecker::launchDetachedInstaller(const juce::File& stagedBundleDir,
                                            const juce::File& installedBundleDir)
{
#if JUCE_WINDOWS
    auto scriptsDir = stagingDir();
    scriptsDir.createDirectory();
    auto bat = scriptsDir.getChildFile("apply_update.cmd");

    // Poll-copy: robocopy mirrors the new bundle over the old one; it returns
    // >= 8 while the DLL is still locked by a running DAW, so we retry until the
    // DAW is closed, then clean up and exit.
    juce::String s;
    s << "@echo off\r\n"
      << "setlocal\r\n"
      << "set SRC=" << stagedBundleDir.getFullPathName().quoted() << "\r\n"
      << "set DST=" << installedBundleDir.getFullPathName().quoted() << "\r\n"
      << ":loop\r\n"
      << "timeout /t 3 /nobreak >nul\r\n"
      << "robocopy %SRC% %DST% /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >nul\r\n"
      << "if %errorlevel% geq 8 goto loop\r\n"
      << "rmdir /s /q " << stagedBundleDir.getFullPathName().quoted() << " >nul 2>&1\r\n"
      << "del /q \"%~f0\" >nul 2>&1\r\n"
      << "endlocal\r\n";

    if (! bat.replaceWithText(s))
        return false;

    // Launch fully detached so it outlives this process / the DAW.
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::wstring cmdLine = L"cmd.exe /c \"" + std::wstring(bat.getFullPathName().toWideCharPointer()) + L"\"";
    std::wstring workDir = scriptsDir.getFullPathName().toWideCharPointer();

    auto spawn = [&](DWORD flags) -> bool
    {
        std::wstring cl = cmdLine;   // CreateProcessW may modify the buffer
        return CreateProcessW(nullptr, cl.data(), nullptr, nullptr, FALSE,
                              flags, nullptr, workDir.c_str(), &si, &pi) != 0;
    };

    bool ok = spawn(CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB);
    if (! ok)
        ok = spawn(CREATE_NO_WINDOW | DETACHED_PROCESS);   // job may forbid breakaway

    if (ok)
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
#else
    juce::ignoreUnused(stagedBundleDir, installedBundleDir);
    return false;
#endif
}
