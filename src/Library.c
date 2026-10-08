#ifdef __GNUC__
#define _MINAPPMODEL_H_
#endif

#include <windows.h>
#include <appmodel.h>
#include <wtsapi32.h>

#ifdef __GNUC__
BOOL WINAPI WTSEnumerateProcessesExW(
    HANDLE hServer,
    DWORD *pLevel,
    DWORD SessionId,
    LPWSTR *ppProcessInfo,
    DWORD *pCount
);
#endif

#define MINECRAFT_EXECUTABLE L"Minecraft.Windows.exe"
#define MINECRAFT_WINDOW_CLASS L"Bedrock"

// Bounded waits: the helper must never stay alive forever if the game fails to start.
#define INPUT_IDLE_TIMEOUT_MS 60000
#define WINDOW_TIMEOUT_MS 60000
#define WINDOW_POLL_MS 250

static BOOL IsMinecraftProcess(DWORD processId, LPCWSTR packageFamilyName)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return FALSE;

    WCHAR familyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
    UINT32 familyNameLength = ARRAYSIZE(familyName);

    BOOL result =
        GetPackageFamilyName(process, &familyNameLength, familyName) == ERROR_SUCCESS &&
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

// Returns a handle to the running Minecraft.Windows.exe of this package family in the current session, or NULL.
static HANDLE FindMinecraftProcess(LPCWSTR packageFamilyName)
{
    DWORD level = 0;
    DWORD count = 0;
    PWTS_PROCESS_INFOW processes = NULL;

    if (!WTSEnumerateProcessesExW(
            WTS_CURRENT_SERVER_HANDLE,
            &level,
            WTS_CURRENT_SESSION,
            (LPWSTR *)&processes,
            &count))
        return NULL;

    HANDLE result = NULL;

    for (DWORD i = 0; i < count; ++i)
    {
        WTS_PROCESS_INFOW *process = &processes[i];

        if (!process->pProcessName ||
            CompareStringOrdinal(
                MINECRAFT_EXECUTABLE,
                -1,
                process->pProcessName,
                -1,
                TRUE) != CSTR_EQUAL)
            continue;

        if (!IsMinecraftProcess(process->ProcessId, packageFamilyName))
            continue;

        result = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION |
            SYNCHRONIZE,
            FALSE,
            process->ProcessId
        );

        if (result)
            break;
    }

    WTSFreeMemoryExW(
        WTSTypeProcessInfoLevel0,
        processes,
        count
    );

    return result;
}

// Skips argv[0] of a command line using the CommandLineToArgvW rules for the program name.
static LPCWSTR SkipProgramName(LPCWSTR commandLine)
{
    LPCWSTR arguments = commandLine;

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

    return arguments;
}

static LPWSTR Concat(LPCWSTR first, LPCWSTR second, LPCWSTR third, LPCWSTR fourth)
{
    LPCWSTR parts[] = { first, second, third, fourth };
    SIZE_T length = 1;

    for (int i = 0; i < ARRAYSIZE(parts); ++i)
        length += lstrlenW(parts[i]);

    LPWSTR buffer = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    if (!buffer)
        return NULL;

    LPWSTR cursor = buffer;
    for (int i = 0; i < ARRAYSIZE(parts); ++i)
    {
        for (LPCWSTR source = parts[i]; *source; ++source)
            *cursor++ = *source;
    }

    *cursor = L'\0';
    return buffer;
}

// Brings this package's Minecraft windows to the foreground. Returns TRUE when at least one was found.
static BOOL ActivateMinecraftWindows(LPCWSTR packageFamilyName)
{
    BOOL found = FALSE;
    HWND window = NULL;

    while ((window = FindWindowExW(
        NULL,
        window,
        MINECRAFT_WINDOW_CLASS,
        NULL)))
    {
        DWORD processId = 0;

        if (!GetWindowThreadProcessId(window, &processId))
            continue;

        if (!IsWindowVisible(window) || !IsMinecraftProcess(processId, packageFamilyName))
            continue;

        SwitchToThisWindow(window, TRUE);
        found = TRUE;
    }

    return found;
}

// Waits (bounded) until the game shows its window, then activates it, like the PC Bootstrapper does.
static VOID WaitForMinecraftWindow(HANDLE process, LPCWSTR packageFamilyName)
{
    WaitForInputIdle(process, INPUT_IDLE_TIMEOUT_MS);

    for (DWORD waited = 0; waited < WINDOW_TIMEOUT_MS; waited += WINDOW_POLL_MS)
    {
        if (ActivateMinecraftWindows(packageFamilyName))
            return;

        if (WaitForSingleObject(process, WINDOW_POLL_MS) == WAIT_OBJECT_0)
            return;
    }
}

// Starts Minecraft.Windows.exe from the package root with this helper's arguments (e.g. protocol activation URIs).
static DWORD LaunchMinecraft(LPCWSTR packagePath, LPCWSTR packageFamilyName)
{
    LPWSTR applicationName = Concat(packagePath, L"\\", MINECRAFT_EXECUTABLE, L"");
    if (!applicationName)
        return ERROR_OUTOFMEMORY;

    LPCWSTR arguments = SkipProgramName(GetCommandLineW());

    // argv[0] must be the program itself so the game sees the forwarded arguments unchanged.
    LPWSTR commandLine = *arguments
        ? Concat(L"\"", applicationName, L"\" ", arguments)
        : Concat(L"\"", applicationName, L"\"", L"");

    if (!commandLine)
    {
        HeapFree(GetProcessHeap(), 0, applicationName);
        return ERROR_OUTOFMEMORY;
    }

    // SecureZeroMemory is inline (no C runtime memset is linked into this DLL).
    STARTUPINFOW startupInfo;
    SecureZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo;
    SecureZeroMemory(&processInfo, sizeof(processInfo));

    BOOL created = CreateProcessW(
        applicationName,
        commandLine,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        packagePath,
        &startupInfo,
        &processInfo
    );

    DWORD error = created ? ERROR_SUCCESS : GetLastError();

    HeapFree(GetProcessHeap(), 0, commandLine);
    HeapFree(GetProcessHeap(), 0, applicationName);

    if (!created)
        return error;

    CloseHandle(processInfo.hThread);

    AllowSetForegroundWindow(processInfo.dwProcessId);
    WaitForMinecraftWindow(processInfo.hProcess, packageFamilyName);

    CloseHandle(processInfo.hProcess);
    return ERROR_SUCCESS;
}

static LPWSTR GetCurrentPackageRoot(VOID)
{
    UINT32 length = 0;

    if (GetCurrentPackagePath(&length, NULL) != ERROR_INSUFFICIENT_BUFFER)
        return NULL;

    LPWSTR path = (LPWSTR)HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    if (!path)
        return NULL;

    if (GetCurrentPackagePath(&length, path) != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, path);
        return NULL;
    }

    return path;
}

static DWORD WINAPI ThreadProc(LPVOID parameter)
{
    UNREFERENCED_PARAMETER(parameter);

    DWORD exitCode = ERROR_SUCCESS;
    LPWSTR packagePath = NULL;
    HANDLE mutex = NULL;
    HANDLE process = NULL;

    WCHAR packageFamilyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
    UINT32 packageFamilyNameLength = ARRAYSIZE(packageFamilyName);

    // The helper runs inside the package the launcher activated; outside a package there is nothing to launch.
    exitCode = GetCurrentPackageFamilyName(
        &packageFamilyNameLength,
        packageFamilyName);

    if (exitCode != ERROR_SUCCESS)
        goto cleanup;

    packagePath = GetCurrentPackageRoot();
    if (!packagePath)
    {
        exitCode = ERROR_NOT_FOUND;
        goto cleanup;
    }

    // The game's working directory is the root of its install location.
    if (!SetCurrentDirectoryW(packagePath))
    {
        exitCode = GetLastError();
        goto cleanup;
    }

    mutex = CreateMutexW(
        NULL,
        FALSE,
        packageFamilyName
    );

    if (!mutex)
    {
        exitCode = GetLastError();
        goto cleanup;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // Another helper of this package is starting the game: just surface whatever window exists.
        ActivateMinecraftWindows(packageFamilyName);
        goto cleanup;
    }

    process = FindMinecraftProcess(packageFamilyName);

    if (process)
    {
        // Single instance: never start a second game, bring the running one to the foreground.
        WaitForMinecraftWindow(process, packageFamilyName);
        CloseHandle(process);
    }
    else
    {
        exitCode = LaunchMinecraft(packagePath, packageFamilyName);
    }

cleanup:
    if (mutex)
        CloseHandle(mutex);

    if (packagePath)
        HeapFree(GetProcessHeap(), 0, packagePath);

    TerminateProcess(GetCurrentProcess(), exitCode);
    return exitCode;
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
