/******************************************************************************
 * Hookshot
 *   General-purpose library for injecting DLLs and hooking function calls.
 ******************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2019-2020
 **************************************************************************//**
 * @file CodeInjector.h
 *   Implementation of code injection, execution, and synchronization.
 *   This file is the only interface to assembly-written code.
 *   Changes to behavior of the assembly code must be reflected here too.
 *****************************************************************************/

#include "ApiWindows.h"
#include "CodeInjector.h"
#include "Inject.h"
#include "InjectResult.h"
#include "Strings.h"
#include "TemporaryBuffer.h"

#include <cstddef>
#include <psapi.h>


namespace Hookshot
{
    // -------- CONSTRUCTION AND DESTRUCTION ------------------------------- //
    // See "CodeInjector.h" for documentation.

    CodeInjector::CodeInjector(void* const baseAddressCode, void* const baseAddressData, const bool cleanupCodeBuffer, const bool cleanupDataBuffer, void* const entryPoint, const size_t sizeCode, const size_t sizeData, const HANDLE injectedProcess, const HANDLE injectedProcessMainThread) : baseAddressCode(baseAddressCode), baseAddressData(baseAddressData), cleanupCodeBuffer(cleanupCodeBuffer), cleanupDataBuffer(cleanupDataBuffer), entryPoint(entryPoint), sizeCode(sizeCode), sizeData(sizeData), injectedProcess(injectedProcess), injectedProcessMainThread(injectedProcessMainThread), injectInfo()
    {
        // Nothing to do here.
    }


    // -------- INSTANCE METHODS ------------------------------------------- //
    // See "CodeInjector.h" for documentation.

    EInjectResult CodeInjector::SetAndRun(void)
    {
        EInjectResult result = Check();

        if (EInjectResult::InjectResultSuccess == result)
            result = Set();

        if (EInjectResult::InjectResultSuccess == result)
            result = Run();

        if (EInjectResult::InjectResultSuccess == result)
            result = UnsetTrampoline();
        
        return result;
    }


    // -------- HELPERS ---------------------------------------------------- //
    // See "CodeInjector.h" for documentation.

    EInjectResult CodeInjector::Check(void) const
    {
        if (EInjectResult::InjectResultSuccess != injectInfo.InitializationResult())
            return injectInfo.InitializationResult();
        
        if (GetTrampolineCodeSize() > sizeof(oldCodeAtTrampoline))
            return EInjectResult::InjectResultErrorInsufficientTrampolineSpace;
        
        if (GetRequiredCodeSize() > sizeCode)
            return EInjectResult::InjectResultErrorInsufficientCodeSpace;

        if (GetRequiredDataSize() > sizeData)
            return EInjectResult::InjectResultErrorInsufficientDataSpace;

        if ((NULL == baseAddressCode) || (NULL == baseAddressData) || (NULL == entryPoint) || (INVALID_HANDLE_VALUE == injectedProcess) || (INVALID_HANDLE_VALUE == injectedProcessMainThread))
            return EInjectResult::InjectResultErrorInternalInvalidParams;
        
        return EInjectResult::InjectResultSuccess;
    }

    // --------

    size_t CodeInjector::GetRequiredCodeSize(void) const
    {
        return (size_t)injectInfo.GetInjectCodeEnd() - (size_t)injectInfo.GetInjectCodeStart();
    }

    // --------

    size_t CodeInjector::GetRequiredDataSize(void) const
    {
        return sizeof(SInjectData);
    }

    // --------

    size_t CodeInjector::GetTrampolineCodeSize(void) const
    {
        return (size_t)injectInfo.GetInjectTrampolineEnd() - (size_t)injectInfo.GetInjectTrampolineStart();
    }

    // --------
    
    bool CodeInjector::LocateFunctions(void*& addrGetLastError, void*& addrGetProcAddress, void*& addrLoadLibraryA) const
    {
        HMODULE moduleGetLastError = NULL;
        HMODULE moduleGetProcAddress = NULL;
        HMODULE moduleLoadLibraryA = NULL;

        TemporaryBuffer<TCHAR> moduleFilenameGetLastError;
        TemporaryBuffer<TCHAR> moduleFilenameGetProcAddress;
        TemporaryBuffer<TCHAR> moduleFilenameLoadLibraryA;
        MODULEINFO moduleInfo;

        // Get module handles for the desired functions in the current process.
        // The same DLL will export them in the target process, but the base address might not be the same.
        if (FALSE == GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)GetLastError, &moduleGetLastError))
            return false;

        if (FALSE == GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)GetProcAddress, &moduleGetProcAddress))
            return false;

        if (FALSE == GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)LoadLibraryA, &moduleLoadLibraryA))
            return false;

        // Compute the relative addresses of each desired function with respect to the base address of its associated DLL.
        size_t offsetGetLastError = (size_t)-1;
        size_t offsetGetProcAddress = (size_t)-1;
        size_t offsetLoadLibraryA = (size_t)-1;
        
        if (FALSE == GetModuleInformation(GetCurrentProcess(), moduleGetLastError, &moduleInfo, sizeof(moduleInfo)))
            return false;

        offsetGetLastError = (size_t)GetLastError - (size_t)moduleInfo.lpBaseOfDll;

        if ((moduleGetProcAddress != moduleGetLastError) && (FALSE == GetModuleInformation(GetCurrentProcess(), moduleGetProcAddress, &moduleInfo, sizeof(moduleInfo))))
            return false;

        offsetGetProcAddress = (size_t)GetProcAddress - (size_t)moduleInfo.lpBaseOfDll;

        if ((moduleLoadLibraryA != moduleGetProcAddress) && (FALSE == GetModuleInformation(GetCurrentProcess(), moduleGetProcAddress, &moduleInfo, sizeof(moduleInfo))))
            return false;

        offsetLoadLibraryA = (size_t)LoadLibraryA - (size_t)moduleInfo.lpBaseOfDll;

        // Compute the full path names for each module that offers the required functions.
        if (0 == GetModuleFileName(moduleGetLastError, moduleFilenameGetLastError, moduleFilenameGetLastError.Count()))
            return false;

        if (0 == GetModuleFileName(moduleGetProcAddress, moduleFilenameGetProcAddress, moduleFilenameGetProcAddress.Count()))
            return false;

        if (0 == GetModuleFileName(moduleLoadLibraryA, moduleFilenameLoadLibraryA, moduleFilenameLoadLibraryA.Count()))
            return false;

        // Enumerate all of the modules in the target process.
        // This approach is necessary because GetModuleHandle(Ex) cannot act on processes other than the calling process.
        TemporaryBuffer<HMODULE> loadedModules;
        DWORD numLoadedModules = 0;

        if (FALSE == EnumProcessModules(injectedProcess, loadedModules, loadedModules.Size(), &numLoadedModules))
            return false;

        numLoadedModules /= sizeof(HMODULE);

        // For each loaded module, see if its full name matches one of the desired modules and, if so, compute the address of the desired function.
        addrGetLastError = NULL;
        addrGetProcAddress = NULL;
        addrLoadLibraryA = NULL;

        for (DWORD modidx = 0; (modidx < numLoadedModules) && ((NULL == addrGetLastError) || (NULL == addrGetProcAddress) || (NULL == addrLoadLibraryA)); ++modidx)
        {
            const HMODULE loadedModule = loadedModules[modidx];
            TemporaryBuffer<TCHAR> loadedModuleName;

            if (0 == GetModuleFileNameEx(injectedProcess, loadedModule, loadedModuleName, loadedModuleName.Count()))
                return false;

            if (NULL == addrGetLastError)
            {
                if (0 == _tcsncmp(moduleFilenameGetLastError, loadedModuleName, loadedModuleName.Count()))
                {
                    if (FALSE == GetModuleInformation(injectedProcess, loadedModule, &moduleInfo, sizeof(moduleInfo)))
                        return false;

                    addrGetLastError = (void*)((size_t)moduleInfo.lpBaseOfDll + offsetGetLastError);
                }
            }

            if (NULL == addrGetProcAddress)
            {
                if (0 == _tcsncmp(moduleFilenameGetProcAddress, loadedModuleName, loadedModuleName.Count()))
                {
                    if (FALSE == GetModuleInformation(injectedProcess, loadedModule, &moduleInfo, sizeof(moduleInfo)))
                        return false;

                    addrGetProcAddress = (void*)((size_t)moduleInfo.lpBaseOfDll + offsetGetProcAddress);
                }
            }

            if (NULL == addrLoadLibraryA)
            {
                if (0 == _tcsncmp(moduleFilenameLoadLibraryA, loadedModuleName, loadedModuleName.Count()))
                {
                    if (FALSE == GetModuleInformation(injectedProcess, loadedModule, &moduleInfo, sizeof(moduleInfo)))
                        return false;

                    addrLoadLibraryA = (void*)((size_t)moduleInfo.lpBaseOfDll + offsetLoadLibraryA);
                }
            }
        }

        return ((NULL != addrGetLastError) && (NULL != addrGetProcAddress) && (NULL != addrLoadLibraryA));
    }
    
    // --------

    EInjectResult CodeInjector::Run(void)
    {
        injectInit(injectedProcess, baseAddressData);
        
        // Allow the injected code to start running.
        if (1 != ResumeThread(injectedProcessMainThread))
            return EInjectResult::InjectResultErrorRunFailedResumeThread;
        
        // Synchronize with the injected code.
        if (false == injectSync())
            return EInjectResult::InjectResultErrorRunFailedSync;

        // Fill in some values that the injected process needs to perform required operations.
        {
            void* addrGetLastError;
            void* addrGetProcAddress;
            void* addrLoadLibraryA;

            if (true != LocateFunctions(addrGetLastError, addrGetProcAddress, addrLoadLibraryA))
                return EInjectResult::InjectResultErrorCannotLocateRequiredFunctions;

            if (false == injectDataFieldWrite(funcGetLastError, &addrGetLastError))
                return EInjectResult::InjectResultErrorCannotWriteRequiredFunctionLocations;

            if (false == injectDataFieldWrite(funcGetProcAddress, &addrGetProcAddress))
                return EInjectResult::InjectResultErrorCannotWriteRequiredFunctionLocations;

            if (false == injectDataFieldWrite(funcLoadLibraryA, &addrLoadLibraryA))
                return EInjectResult::InjectResultErrorCannotWriteRequiredFunctionLocations;
        }
        
        // Synchronize with the injected code.
        if (false == injectSync())
            return EInjectResult::InjectResultErrorRunFailedSync;
        
        // Wait for the injected code to reach completion and synchronize with it.
        // Once the injected code reaches this point, put the thread to sleep and then allow it to advance.
        // This way, upon waking, the thread will advance past the barrier and execute the process as normal.
        if (false == injectSyncWait())
            return EInjectResult::InjectResultErrorRunFailedSync;

        if (0 != SuspendThread(injectedProcessMainThread))
            return EInjectResult::InjectResultErrorRunFailedSuspendThread;

        if (false == injectSyncAdvance())
            return EInjectResult::InjectResultErrorRunFailedSync;

        // Read from the injected process to determine the result of the injection attempt.
        uint32_t injectionResult;
        uint32_t extendedInjectionResult;

        if (false == injectDataFieldRead(injectionResult, &injectionResult))
            return EInjectResult::InjectResultErrorCannotReadStatus;

        if (false == injectDataFieldRead(extendedInjectionResult, &extendedInjectionResult))
            return EInjectResult::InjectResultErrorCannotReadStatus;

        SetLastError((DWORD)extendedInjectionResult);
        return (EInjectResult)injectionResult;
    }

    // --------

    EInjectResult CodeInjector::Set(void)
    {
        SIZE_T numBytes = 0;

        // Back up the code currently at the trampoline's target location.
        if ((FALSE == ReadProcessMemory(injectedProcess, entryPoint, oldCodeAtTrampoline, GetTrampolineCodeSize(), &numBytes)) || (GetTrampolineCodeSize() != numBytes))
            EInjectResult::InjectResultErrorSetFailedRead;
        
        // Write the trampoline code.
        if ((FALSE == WriteProcessMemory(injectedProcess, entryPoint, injectInfo.GetInjectTrampolineStart(), GetTrampolineCodeSize(), &numBytes)) || (GetTrampolineCodeSize() != numBytes) || (FALSE == FlushInstructionCache(injectedProcess, entryPoint, GetTrampolineCodeSize())))
            EInjectResult::InjectResultErrorSetFailedWrite;

        // Place the address of the main code entry point into the trampoline code at the correct location.
        {
            void* const targetAddress = (void*)((size_t)entryPoint + ((size_t)injectInfo.GetInjectTrampolineAddressMarker() - (size_t)injectInfo.GetInjectTrampolineStart()) - sizeof(size_t));
            const size_t sourceData = (size_t)baseAddressCode + ((size_t)injectInfo.GetInjectCodeBegin() - (size_t)injectInfo.GetInjectCodeStart());
            
            if ((FALSE == WriteProcessMemory(injectedProcess, targetAddress, &sourceData, sizeof(sourceData), &numBytes)) || (sizeof(sourceData) != numBytes) || (FALSE == FlushInstructionCache(injectedProcess, targetAddress, sizeof(sourceData))))
                EInjectResult::InjectResultErrorSetFailedWrite;
        }

        // Write the main code.
        if ((FALSE == WriteProcessMemory(injectedProcess, baseAddressCode, injectInfo.GetInjectCodeStart(), GetRequiredCodeSize(), &numBytes)) || (GetRequiredCodeSize() != numBytes) || (FALSE == FlushInstructionCache(injectedProcess, baseAddressCode, GetRequiredCodeSize())))
            EInjectResult::InjectResultErrorSetFailedWrite;

        // Place the pointer to the data region into the correct spot in the code region.
        {
            void* const targetAddress = baseAddressCode;
            const size_t sourceData = (size_t)baseAddressData;

            if ((FALSE == WriteProcessMemory(injectedProcess, targetAddress, &sourceData, sizeof(sourceData), &numBytes)) || (sizeof(sourceData) != numBytes) || (FALSE == FlushInstructionCache(injectedProcess, targetAddress, sizeof(sourceData))))
                EInjectResult::InjectResultErrorSetFailedWrite;
        }

        // Initialize the data region.
        {
            SInjectData injectData;
            char injectDataStrings[InjectInfo::kMaxInjectBinaryFileSize - sizeof(injectData)];

            memset((void*)&injectData, 0, sizeof(injectData));
            memset((void*)&injectDataStrings, 0, sizeof(injectDataStrings));

            injectData.isDebuggerAttached = (0 == IsDebuggerPresent() ? 0 : 1);
            injectData.injectionResultCodeSuccess = EInjectResult::InjectResultSuccess;
            injectData.injectionResultCodeLoadLibraryFailed = EInjectResult::InjectResultErrorCannotLoadLibrary;
            injectData.injectionResultCodeGetProcAddressFailed = EInjectResult::InjectResultErrorMalformedLibrary;
            injectData.injectionResultCodeInitializationFailed = EInjectResult::InjectResultErrorLibraryInitFailed;
            injectData.injectionResult = EInjectResult::InjectResultFailure;

            strcpy_s(injectDataStrings, Strings::kStrLibraryInitializationProcName.data());

#ifdef UNICODE
            if (0 != wcstombs_s(NULL, &injectDataStrings[Strings::kStrLibraryInitializationProcName.length() + 1], sizeof(injectDataStrings) - (Strings::kStrLibraryInitializationProcName.length() + 1) - 1, Strings::kStrHookshotDynamicLinkLibraryFilename.data(), sizeof(injectDataStrings) - (Strings::kStrLibraryInitializationProcName.length() + 1) - 2))
                return EInjectResult::InjectResultErrorCannotGenerateLibraryFilename;
#else
            if (false == Strings::FillHookshotDynamicLinkLibraryFilename(&injectDataStrings[Strings::kLenLibraryInitializationProcName], sizeof(injectDataStrings) - Strings::kLenLibraryInitializationProcName - 1))
                return EInjectResult::InjectResultErrorCannotGenerateLibraryFilename;
#endif

            injectData.strLibraryName = (const char*)((size_t)baseAddressData + sizeof(injectData) + (Strings::kStrLibraryInitializationProcName.length() + 1));
            injectData.strProcName = (const char*)((size_t)baseAddressData + sizeof(injectData));
            
            // Figure out which addresses need to be cleaned up.
            // These buffers will be freed once injection is complete.
            {
                unsigned int cleanupIndex = 0;

                if (true == cleanupCodeBuffer)
                    injectData.cleanupBaseAddress[cleanupIndex++] = baseAddressCode;

                if (true == cleanupDataBuffer)
                    injectData.cleanupBaseAddress[cleanupIndex++] = baseAddressData;
            }
            
            if ((FALSE == WriteProcessMemory(injectedProcess, baseAddressData, &injectData, sizeof(injectData), &numBytes)) || (sizeof(injectData) != numBytes))
                EInjectResult::InjectResultErrorSetFailedWrite;

            if ((FALSE == WriteProcessMemory(injectedProcess, (void*)((size_t)baseAddressData + sizeof(injectData)), injectDataStrings, sizeof(injectDataStrings), &numBytes)) || (sizeof(injectDataStrings) != numBytes))
                EInjectResult::InjectResultErrorSetFailedWrite;
        }
        
        return EInjectResult::InjectResultSuccess;
    }

    // --------

    EInjectResult CodeInjector::UnsetTrampoline(void)
    {
        SIZE_T numBytes = 0;

        if ((FALSE == WriteProcessMemory(injectedProcess, entryPoint, oldCodeAtTrampoline, GetTrampolineCodeSize(), &numBytes)) || (GetTrampolineCodeSize() != numBytes) || (FALSE == FlushInstructionCache(injectedProcess, entryPoint, GetTrampolineCodeSize())))
            return EInjectResult::InjectResultErrorUnsetFailed;
        
        return EInjectResult::InjectResultSuccess;
    }
}
