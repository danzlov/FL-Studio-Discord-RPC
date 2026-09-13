#define FLDRPC_PROXY
#include "FLDRPC.cpp"

namespace {
DWORD WINAPI TrackerThread(LPVOID parameter) {
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(static_cast<HMODULE>(parameter), modulePath, MAX_PATH);
    std::wstring configPath(modulePath);
    size_t separator = configPath.find_last_of(L"\\/");
    if (separator != std::wstring::npos) configPath.erase(separator + 1);
    configPath += L"config.json";
    RunTracker(WideToUtf8(configPath));
    return 0;
}
}

extern "C" BOOL WINAPI ProxyGetFileVersionInfoA(LPCSTR filename, DWORD handle, DWORD length, LPVOID data) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoA)>("GetFileVersionInfoA");
    return function ? function(filename, handle, length, data) : FALSE;
}

extern "C" BOOL WINAPI ProxyGetFileVersionInfoByHandle(DWORD flags, HANDLE file, LPVOID* data, PDWORD length) {
    using Function = BOOL(WINAPI*)(DWORD, HANDLE, LPVOID*, PDWORD);
    auto function = GetSystemVersionProc<Function>("GetFileVersionInfoByHandle");
    return function ? function(flags, file, data, length) : FALSE;
}

extern "C" BOOL WINAPI ProxyGetFileVersionInfoExA(DWORD flags, LPCSTR filename, DWORD handle, DWORD length, LPVOID data) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoExA)>("GetFileVersionInfoExA");
    return function ? function(flags, filename, handle, length, data) : FALSE;
}

extern "C" BOOL WINAPI ProxyGetFileVersionInfoExW(DWORD flags, LPCWSTR filename, DWORD handle, DWORD length, LPVOID data) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoExW)>("GetFileVersionInfoExW");
    return function ? function(flags, filename, handle, length, data) : FALSE;
}

extern "C" DWORD WINAPI ProxyGetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoSizeA)>("GetFileVersionInfoSizeA");
    return function ? function(filename, handle) : 0;
}

extern "C" DWORD WINAPI ProxyGetFileVersionInfoSizeExA(DWORD flags, LPCSTR filename, LPDWORD handle) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoSizeExA)>("GetFileVersionInfoSizeExA");
    return function ? function(flags, filename, handle) : 0;
}

extern "C" DWORD WINAPI ProxyGetFileVersionInfoSizeExW(DWORD flags, LPCWSTR filename, LPDWORD handle) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoSizeExW)>("GetFileVersionInfoSizeExW");
    return function ? function(flags, filename, handle) : 0;
}

extern "C" DWORD WINAPI ProxyGetFileVersionInfoSizeW(LPCWSTR filename, LPDWORD handle) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoSizeW)>("GetFileVersionInfoSizeW");
    return function ? function(filename, handle) : 0;
}

extern "C" BOOL WINAPI ProxyGetFileVersionInfoW(LPCWSTR filename, DWORD handle, DWORD length, LPVOID data) {
    auto function = GetSystemVersionProc<decltype(&GetFileVersionInfoW)>("GetFileVersionInfoW");
    return function ? function(filename, handle, length, data) : FALSE;
}

extern "C" DWORD WINAPI ProxyVerFindFileA(DWORD flags, LPCSTR filename, LPCSTR windowsDirectory, LPCSTR appDirectory, LPSTR currentDirectory, PUINT currentDirectoryLength, LPSTR destinationDirectory, PUINT destinationDirectoryLength) {
    auto function = GetSystemVersionProc<decltype(&VerFindFileA)>("VerFindFileA");
    return function ? function(flags, filename, windowsDirectory, appDirectory, currentDirectory, currentDirectoryLength, destinationDirectory, destinationDirectoryLength) : 0;
}

extern "C" DWORD WINAPI ProxyVerFindFileW(DWORD flags, LPCWSTR filename, LPCWSTR windowsDirectory, LPCWSTR appDirectory, LPWSTR currentDirectory, PUINT currentDirectoryLength, LPWSTR destinationDirectory, PUINT destinationDirectoryLength) {
    auto function = GetSystemVersionProc<decltype(&VerFindFileW)>("VerFindFileW");
    return function ? function(flags, filename, windowsDirectory, appDirectory, currentDirectory, currentDirectoryLength, destinationDirectory, destinationDirectoryLength) : 0;
}

extern "C" DWORD WINAPI ProxyVerInstallFileA(DWORD flags, LPCSTR sourceFilename, LPCSTR destinationFilename, LPCSTR sourceDirectory, LPCSTR destinationDirectory, LPCSTR currentDirectory, LPSTR temporaryFile, PUINT temporaryFileLength) {
    auto function = GetSystemVersionProc<decltype(&VerInstallFileA)>("VerInstallFileA");
    return function ? function(flags, sourceFilename, destinationFilename, sourceDirectory, destinationDirectory, currentDirectory, temporaryFile, temporaryFileLength) : 0;
}

extern "C" DWORD WINAPI ProxyVerInstallFileW(DWORD flags, LPCWSTR sourceFilename, LPCWSTR destinationFilename, LPCWSTR sourceDirectory, LPCWSTR destinationDirectory, LPCWSTR currentDirectory, LPWSTR temporaryFile, PUINT temporaryFileLength) {
    auto function = GetSystemVersionProc<decltype(&VerInstallFileW)>("VerInstallFileW");
    return function ? function(flags, sourceFilename, destinationFilename, sourceDirectory, destinationDirectory, currentDirectory, temporaryFile, temporaryFileLength) : 0;
}

extern "C" DWORD WINAPI ProxyVerLanguageNameA(DWORD language, LPSTR buffer, DWORD length) {
    auto function = GetSystemVersionProc<decltype(&VerLanguageNameA)>("VerLanguageNameA");
    return function ? function(language, buffer, length) : 0;
}

extern "C" DWORD WINAPI ProxyVerLanguageNameW(DWORD language, LPWSTR buffer, DWORD length) {
    auto function = GetSystemVersionProc<decltype(&VerLanguageNameW)>("VerLanguageNameW");
    return function ? function(language, buffer, length) : 0;
}

extern "C" BOOL WINAPI ProxyVerQueryValueA(LPCVOID block, LPCSTR subBlock, LPVOID* buffer, PUINT length) {
    auto function = GetSystemVersionProc<decltype(&VerQueryValueA)>("VerQueryValueA");
    return function ? function(block, subBlock, buffer, length) : FALSE;
}

extern "C" BOOL WINAPI ProxyVerQueryValueW(LPCVOID block, LPCWSTR subBlock, LPVOID* buffer, PUINT length) {
    auto function = GetSystemVersionProc<decltype(&VerQueryValueW)>("VerQueryValueW");
    return function ? function(block, subBlock, buffer, length) : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, TrackerThread, instance, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
