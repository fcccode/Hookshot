/******************************************************************************
 * Hookshot
 *   General-purpose library for injecting DLLs and hooking function calls.
 ******************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2019-2020
 **************************************************************************//**
 * @file ProcessInjector.cpp
 *   Implementation of all process creation and injection functionality.
 *****************************************************************************/

#include "ApiWindows.h"
#include "RemoteProcessInjector.h"
#include "Strings.h"
#include "TemporaryBuffer.h"
#include "UnicodeTypes.h"


namespace Hookshot
{
    // -------- CLASS METHODS ---------------------------------------------- //
    // See "RemoteProcessInjector.h" for documentation.

    EInjectResult RemoteProcessInjector::RemoteInjectProcess(const HANDLE processHandle, const HANDLE threadHandle, const bool switchArchitecture)
    {
        // Obtain the name of the Hookshot executable to spawn.
        // Hold both the application name and the command-line arguments, enclosing the application name in quotes.
        // At most the argument needs to represent a 64-bit integer in hexadecimal, so two characters per byte, plus a space, an indicator character and a null character.
        const TStdStringView kExecutableFileName = (switchArchitecture ? Strings::kStrHookshotExecutableOtherArchitectureFilename : Strings::kStrHookshotExecutableFilename);
        const size_t kExecutableArgumentMaxCount = 3 + (2 * sizeof(uint64_t));
        const size_t kExecutableCommandLineMaxCount = 3 + kExecutableFileName.length() + kExecutableArgumentMaxCount;

        TStdStringStream executableCommandLine;
        executableCommandLine << _T('\"') << kExecutableFileName << _T('\"');

        // Create an anonymous file mapping object backed by the system paging file, and ensure it can be inherited by child processes.
        // This has the effect of creating an anonymous shared memory object.
        // The resulting handle must be passed to the new instance of Hookshot that is spawned.
        SECURITY_ATTRIBUTES sharedMemorySecurityAttributes;
        sharedMemorySecurityAttributes.nLength = sizeof(sharedMemorySecurityAttributes);
        sharedMemorySecurityAttributes.lpSecurityDescriptor = NULL;
        sharedMemorySecurityAttributes.bInheritHandle = TRUE;

        HANDLE sharedMemoryHandle = CreateFileMapping(INVALID_HANDLE_VALUE, &sharedMemorySecurityAttributes, PAGE_READWRITE, 0, sizeof(SRemoteProcessInjectionData), NULL);

        if ((NULL == sharedMemoryHandle) || (INVALID_HANDLE_VALUE == sharedMemoryHandle))
            return EInjectResult::InjectResultErrorInterProcessCommunicationFailed;

        SRemoteProcessInjectionData* const sharedInfo = (SRemoteProcessInjectionData*)MapViewOfFile(sharedMemoryHandle, FILE_MAP_ALL_ACCESS, 0, 0, 0);

        if (NULL == sharedInfo)
            return EInjectResult::InjectResultErrorInterProcessCommunicationFailed;

        // Append the command-line argument to pass to the new Hookshot instance and convert to a mutable string, as required by CreateProcess.
        executableCommandLine << _T(' ') << Strings::kCharCmdlineIndicatorFileMappingHandle << std::hex << (uint64_t)sharedMemoryHandle;
        
        TemporaryBuffer<TCHAR> executableCommandLineMutableString;
        if (0 != _tcscpy_s(executableCommandLineMutableString, executableCommandLineMutableString.Count(), executableCommandLine.str().c_str()))
            return EInjectResult::InjectResultErrorCannotGenerateExecutableFilename;

        // Create the new instance of Hookshot.
        STARTUPINFO startupInfo;
        PROCESS_INFORMATION processInfo;
        memset((void*)&startupInfo, 0, sizeof(startupInfo));
        memset((void*)&processInfo, 0, sizeof(processInfo));

        if (FALSE == CreateProcess(NULL, executableCommandLineMutableString, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &startupInfo, &processInfo))
            return EInjectResult::InjectResultErrorCreateHookshotProcessFailed;

        // Fill in the required inputs to the new instance of Hookshot.
        HANDLE duplicateProcessHandle = INVALID_HANDLE_VALUE;
        HANDLE duplicateThreadHandle = INVALID_HANDLE_VALUE;

        if ((FALSE == DuplicateHandle(GetCurrentProcess(), processHandle, processInfo.hProcess, &duplicateProcessHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) || (FALSE == DuplicateHandle(GetCurrentProcess(), threadHandle, processInfo.hProcess, &duplicateThreadHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)))
        {
            TerminateProcess(processInfo.hProcess, (UINT)-1);
            return EInjectResult::InjectResultErrorInterProcessCommunicationFailed;
        }

        sharedInfo->processHandle = (uint64_t)duplicateProcessHandle;
        sharedInfo->threadHandle = (uint64_t)duplicateThreadHandle;
        sharedInfo->injectionResult = (uint64_t)EInjectResult::InjectResultFailure;
        sharedInfo->extendedInjectionResult = 0ull;

        // Let the new instance of Hookshot run and wait for it to finish.
        ResumeThread(processInfo.hThread);

        if (WAIT_OBJECT_0 != WaitForSingleObject(processInfo.hProcess, INFINITE))
        {
            TerminateProcess(processInfo.hProcess, (UINT)-1);
            return EInjectResult::InjectResultErrorInterProcessCommunicationFailed;
        }

        // Obtain results from the new instance of Hookshot, clean up, and return.
        DWORD injectExitCode = 0;
        if ((FALSE == GetExitCodeProcess(processInfo.hProcess, &injectExitCode)) || (0 != injectExitCode))
            return EInjectResult::InjectResultErrorInterProcessCommunicationFailed;

        const DWORD extendedResult = (DWORD)sharedInfo->extendedInjectionResult;
        const EInjectResult operationResult = (EInjectResult)sharedInfo->injectionResult;

        UnmapViewOfFile(sharedInfo);
        CloseHandle(sharedMemoryHandle);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);

        SetLastError(extendedResult);
        return operationResult;
    }
}
