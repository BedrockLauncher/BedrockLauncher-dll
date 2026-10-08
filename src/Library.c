#define _MINAPPMODEL_H_

#include <windows.h>
#include <appmodel.h>
#include <winternl.h>
#include <wtsapi32.h>

BOOL WTSEnumerateProcessesExW(
    HANDLE hServer,
    DWORD *pLevel,
    DWORD SessionId,
    LPWSTR *ppProcessInfo,
    DWORD *pCount
);

static BOOL IsMinecraftProcess(DWORD processId, LPCWSTR packageFamilyName)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return FALSE;

    WCHAR familyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
    UINT familyNameLength = ARRAYSIZE(familyName);

    BOOL result =
        !GetPackageFamilyName(process, &familyNameLength, familyName) &&
        CompareStringOrdinal(
            packageFamilyName,
            -1,
            familyName,
            -1,
            TRUE
        ) == CSTR_EQUAL;

    CloseHandle(process);
    return result;
}

static HANDLE FindMinecraftProcess(LPCWSTR packageFamilyName)
{
    DWORD level = 0;
    DWORD count = 0;
    PWTS_PROCESS_INFOW processes = NULL;

    if (!WTSEnumerateProcessesExW(
            WTS_CURRENT_SERVER,
            &level,
            WTS_CURRENT_SESSION,
            (LPWSTR *)&processes,
            &count))
        return NULL;

    HANDLE result = NULL;

    for (DWORD i = 0; i < count; ++i)
    {
        WTS_PROCESS_INFOW *process = &processes[i];

        if (CompareStringOrdinal(
                L"Minecraft.Windows.exe",
                -1,
                process->pProcessName,
                -1,
                TRUE) != CSTR_EQUAL)
            continue;

        HANDLE handle = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION |
            SYNCHRONIZE,
            FALSE,
            process->ProcessId
        );

        if (!handle)
            continue;

        WCHAR familyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
        UINT familyNameLength = ARRAYSIZE(familyName);

        if (!GetPackageFamilyName(
                handle,
                &familyNameLength,
                familyName) &&
            CompareStringOrdinal(
                packageFamilyName,
                -1,
                familyName,
                -1,
                TRUE) == CSTR_EQUAL)
        {
            result = handle;
            break;
        }

        CloseHandle(handle);
    }

    WTSFreeMemoryExW(
        WTSTypeProcessInfoLevel0,
        processes,
        count
    );

    return result;
}

static BOOL LaunchMinecraft(void)
{
    RTL_USER_PROCESS_PARAMETERS *params =
        NtCurrentTeb()->ProcessEnvironmentBlock->ProcessParameters;

    if (!params ||
        !params->CommandLine.Buffer ||
        !params->ImagePathName.Buffer)
        return FALSE;

    SIZE_T imagePathLength =
        wcslen(params->ImagePathName.Buffer);

    LPCWSTR arguments =
        params->CommandLine.Buffer + imagePathLength;

    if (*arguments == L'"')
    {
        ++arguments;

        while (*arguments && *arguments != L'"')
            ++arguments;

        if (*arguments == L'"')
            ++arguments;
    }
    else
    {
        while (*arguments && *arguments != L' ' && *arguments != L'\t')
            ++arguments;
    }

    while (*arguments == L' ' || *arguments == L'\t')
        ++arguments;

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo = {};

    BOOL created = CreateProcessW(
        L"Minecraft.Windows.exe",
        (LPWSTR)arguments,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &startupInfo,
        &processInfo
    );

    if (!created)
        return FALSE;

    CloseHandle(processInfo.hThread);

    WaitForInputIdle(processInfo.hProcess, INFINITE);
    CloseHandle(processInfo.hProcess);

    return TRUE;
}

static VOID ActivateMinecraftWindows(LPCWSTR packageFamilyName)
{
    HWND window = NULL;

    while ((window = FindWindowExW(
        NULL,
        window,
        L"Bedrock",
        NULL)))
    {
        DWORD processId = 0;

        if (!GetWindowThreadProcessId(window, &processId))
            continue;

        if (IsMinecraftProcess(processId, packageFamilyName))
            SwitchToThisWindow(window, TRUE);
    }
}

static DWORD WINAPI ThreadProc(LPVOID parameter)
{
    UNREFERENCED_PARAMETER(parameter);

    WCHAR packageFamilyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
    UINT packageFamilyNameLength = ARRAYSIZE(packageFamilyName);

    if (GetCurrentPackageFamilyName(
            &packageFamilyNameLength,
            packageFamilyName) != ERROR_SUCCESS)
        goto cleanup;

    WCHAR packagePath[MAX_PATH] = {};
    UINT packagePathLength = ARRAYSIZE(packagePath);

    if (GetCurrentPackagePath(
            &packagePathLength,
            packagePath) != ERROR_SUCCESS)
        goto cleanup;

    if (!SetCurrentDirectoryW(packagePath))
        goto cleanup;

    HANDLE mutex = CreateMutexW(
        NULL,
        FALSE,
        packageFamilyName
    );

    if (!mutex)
        goto cleanup;

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        goto cleanup;
    }

    HANDLE process = FindMinecraftProcess(packageFamilyName);

    if (process)
    {
        DWORD waitResult = WaitForInputIdle(
            process,
            INFINITE
        );

        if (waitResult != WAIT_FAILED)
        {
            CloseHandle(process);
            process = NULL;
        }
        else
        {
            CloseHandle(process);
            process = NULL;
            LaunchMinecraft();
        }
    }
    else
    {
        LaunchMinecraft();
    }

    ActivateMinecraftWindows(packageFamilyName);

    CloseHandle(mutex);

cleanup:
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

__declspec(dllexport)
VOID GameLaunch(VOID)
{
    Sleep(INFINITE);
}

BOOL WINAPI DllMain(
    HINSTANCE instance,
    DWORD reason,
    LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);

        if (!QueueUserWorkItem(
                ThreadProc,
                NULL,
                WT_EXECUTEDEFAULT))
            return FALSE;
    }

    return TRUE;
}