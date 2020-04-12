/******************************************************************************
 * Hookshot
 *   General-purpose library for injecting DLLs and hooking function calls.
 ******************************************************************************
 * Authored by Samuel Grossman
 * Copyright (c) 2019-2020
 **************************************************************************//**
 * @file ProcessInjector.h
 *   Interface declaration for process creation and injection.
 *****************************************************************************/

#pragma once

#include "ApiWindows.h"
#include "InjectResult.h"
#include "RemoteProcessInjector.h"

#include <cstddef>
#include <winternl.h>


namespace Hookshot
{
    /// Provides all functionality related to creating processes and injecting them with Hookshot code.
    /// All methods are class methods.
    class ProcessInjector
    {
    private:
        // -------- CLASS VARIABLES ------------------------------------------------ //

        /// Module handle for "ntdll.dll" which is loaded dynamically as part of the process injection functionality.
        static HMODULE ntdllModuleHandle;

        /// Procedure address for the "NtQueryInformationProcess" function within "ntdll.dll" which is used to detect information about the newly-created process.
        static NTSTATUS(WINAPI* ntdllQueryInformationProcessProc)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

        /// System allocation granularity.  Captured once and re-used as needed.
        static size_t systemAllocationGranularity;


    public:
        // -------- CONSTRUCTION AND DESTRUCTION --------------------------- //

        /// Default constructor. Should never be invoked.
        ProcessInjector(void) = delete;


        // -------- CLASS METHODS ------------------------------------------ //

        /// Creates a new process using the specified parameters and attempts to inject Hookshot code into it before it is allowed to run.
        /// Refer to Microsoft's documentation on CreateProcess for information on parameters.
        /// @return Indicator of the result of the operation.
        static EInjectResult CreateInjectedProcess(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory, LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation);

        /// Injects a process created by another instance of Hookshot.
        /// Communication between instances occurs by means of shared memory using the handle passed in, including more detailed error information than is directly returned.
        /// @param [in,out] remoteInjectionData Data structure holding information exchanged between this Hookshot process and the Hookshot process that requested injection.
        /// @return `false` if an inter-process communication mechanism failed, `true` otherwise.
        static bool PerformRequestedRemoteInjection(SRemoteProcessInjectionData* const remoteInjectionData);


    private:
        // -------- HELPERS ------------------------------------------------ //
        
        /// Attempts to determine the address of the entry point of the given process.
        /// All addresses used by this method are in the virtual address space of the target process.
        /// @param [in] processHandle Handle to the process for which information is requested.
        /// @param [in] baseAddress Base address of the process' executable image.
        /// @param [out] entryPoint Address of the pointer that receives the entry point address.
        /// @return Indicator of the result of the operation.
        static EInjectResult GetProcessEntryPointAddress(const HANDLE processHandle, const void* const baseAddress, void** const entryPoint);
        
        /// Attempts to determine the base address of the primary executable image for the given process.
        /// All addresses used by this method are in the virtual address space of the target process.
        /// @param [in] processHandle Handle to the process for which information is requested.
        /// @param [out] baseAddress Address of the pointer that receives the image base address.
        /// @return Indicator of the result of the operation.
        static EInjectResult GetProcessImageBaseAddress(const HANDLE processHandle, void** const baseAddress);

        /// Attempts to inject a process with Hookshot code.
        /// @param [in] processHandle Handle to the process to inject.
        /// @param [in] threadHandle Handle to the main thread of the process to inject.
        /// @param [in] enableDebugFeatures If `true`, signals to the injected process that a debugger is present, so certain debug features should be enabled.
        /// @return Indicator of the result of the operation.
        static EInjectResult InjectProcess(const HANDLE processHandle, const HANDLE threadHandle, const bool enableDebugFeatures);

        /// Verifies that the architecture of the target process matches the architecture of this running binary.
        /// @param [in] processHandle Handle to the process to check.
        /// @return Indicator of the result of the operation.
        static EInjectResult VerifyMatchingProcessArchitecture(const HANDLE processHandle);
    };
}
