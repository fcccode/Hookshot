@echo off
rem +--------------------------------------------------------------------------
rem | Hookshot
rem |   General-purpose library for injecting DLLs and hooking function calls.
rem +--------------------------------------------------------------------------
rem | Authored by Samuel Grossman
rem | Copyright (c) 2019-2020
rem +--------------------------------------------------------------------------
rem | PackageRelease.bat
rem |   Script for packaging up a release.  To be executed manually after
rem |   building the Release configuration for both Win32 and x64 platforms.
rem +--------------------------------------------------------------------------

set project_name=Hookshot
set project_has_sdk=yes

set files_release=LICENSE *.md Output\Release\Hookshot.32.exe Output\Release\Hookshot.64.exe Output\Release\Hookshot.32.dll Output\Release\Hookshot.64.dll
set files_sdk_lib=Output\Release\Hookshot.32.lib Output\Release\Hookshot.64.lib
set files_sdk_include=Include\Hookshot\*.h

rem ---------------------------------------------------------------------------

set script_path=%~dp0
set release_ver=%1
set output_dir=%~f2

if "%release_ver%"=="" (
    echo Missing release version!
    exit /b
)

if "%output_dir%"=="" (
    echo Missing output directory!
    exit /b
)

set output_dir=%output_dir%\%project_name%-v%release_ver%

echo Releasing %project_name% v%release_ver% to %output_dir%.
choice /M "Proceed?"
if not %ERRORLEVEL%==1 exit /b

if exist %output_dir% (
    echo Output directory exists and will be overwritten.
    choice /M "Still proceed?"
    if not %ERRORLEVEL%==1 exit /b 55
    rd /S /Q %output_dir%
)

if %ERRORLEVEL%==55 exit /b

pushd %script_dir%
set files_are_missing=no
for %%F in (%files_release% %files_sdk_lib% %files_sdk_include%) do (
    if not exist %%F (
        echo Missing file: %%F
        set files_are_missing=yes
    )
)
popd

if "yes"=="%files_are_missing%" exit /b

pushd %script_dir%
md %output_dir%
for %%F in (%files_release%) do (
    echo %%F
    copy %%F %output_dir%
)

if not ""=="%project_has_sdk%" (
    md %output_dir%\SDK
    
    if not ""=="%files_sdk_lib%" (
        md %output_dir%\SDK\Lib
        for %%F in (%files_sdk_lib%) do (
            echo %%F
            copy %%F %output_dir%\SDK\Lib
        )
    )

    if not ""=="%files_sdk_include%" (
        md %output_dir%\SDK\Include
        md %output_dir%\SDK\Include\%project_name%
        for %%F in (%files_sdk_include%) do (
            echo %%F
            copy %%F %output_dir%\SDK\Include\%project_name%
        )
    )
)
popd
