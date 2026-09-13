#include <Windows.h>
#include <TlHelp32.h>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <thread>
#include <fstream>
#include "./external/json/json.hpp"
#include <winver.h>
#include <unordered_map>

#include "./external/discord_rpc/include/discord_rpc.h"
#pragma comment(lib, "discord-rpc.lib")

using json = nlohmann::json;

HMODULE GetSystemVersionModule() {
    static HMODULE module = [] {
        wchar_t path[MAX_PATH] = {};
        UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (!length || length + 13 >= MAX_PATH) return static_cast<HMODULE>(nullptr);
        wcscat_s(path, L"\\version.dll");
        return LoadLibraryW(path);
    }();
    return module;
}

template<typename T>
T GetSystemVersionProc(const char* name) {
    HMODULE module = GetSystemVersionModule();
    T function = module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
    if (!function) SetLastError(ERROR_PROC_NOT_FOUND);
    return function;
}

// ---------------------- Config ----------------------
struct Config {
    std::string discordAppId;
    std::string largeImageKey;
    int refreshInterval = 2;
    std::string defaultProjectName = "Untitled";
    std::string playingImageKey = "play";
    std::string stoppedImageKey = "stop";
    std::string presenceDetails;
    std::string presenceState;
};

Config g_config;

bool LoadConfig(const std::string& path, Config& cfg) {
    cfg.discordAppId = "1451215754035855514";
    cfg.largeImageKey = "flstudio";
    cfg.refreshInterval = 2;
    cfg.defaultProjectName = "Untitled";
    cfg.playingImageKey = "play";
    cfg.stoppedImageKey = "stop";
    cfg.presenceDetails = "Project: {Project} | BPM: {BPM} | Pitch: {Pitch} | Metronome: {Metronome} | Mode: {Mode}";
    cfg.presenceState = "Playback: {Status} | Position: {Position}";

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        json j;
        file >> j;

        cfg.discordAppId = j.value("discordAppId", cfg.discordAppId);
        cfg.largeImageKey = j.value("largeImageKey", cfg.largeImageKey);
        cfg.refreshInterval = j.value("refreshInterval", cfg.refreshInterval);
        cfg.defaultProjectName = j.value("defaultProjectName", cfg.defaultProjectName);
        cfg.playingImageKey = j.value("playingImageKey", cfg.playingImageKey);
        cfg.stoppedImageKey = j.value("stoppedImageKey", cfg.stoppedImageKey);

        if (j.contains("presence")) {
            cfg.presenceDetails = j["presence"].value("details", cfg.presenceDetails);
            cfg.presenceState = j["presence"].value("state", cfg.presenceState);
        }
    }
    catch (...) {
        std::cout << "JSON format error, using defaults.\n";
    }

    return true;
}

// ---------------------- Memory scanning ----------------------
enum DataType {
    TYPE_FLOAT,
    TYPE_INT,
    TYPE_SHORT,
    TYPE_BYTE,
    TYPE_POINTER_TO_INT,
    TYPE_POINTER_TO_SHORT,
    TYPE_POINTER_TO_BYTE
};

struct Signature {
	const char* pattern;
	int ripOffset;
	int instrLen;
	int postOffset;
	DataType type;
};

struct Pattern {
    std::string name;
	std::vector<Signature> signatures; // take into consideration older and newer versions
};

struct MemHandle {
    HANDLE proc{};
    uintptr_t addr{};

    MemHandle add(ptrdiff_t offset) const { return { proc, addr + offset }; }
    MemHandle sub(ptrdiff_t offset) const { return { proc, addr - offset }; }

    MemHandle rip(int instrLen) const {
        int32_t disp = 0;
        ReadProcessMemory(proc, (LPCVOID)addr, &disp, sizeof(disp), nullptr);
        return { proc, (addr - 2) + instrLen + disp };
    }

    template<typename T>
    T as() const {
        T val{};
        ReadProcessMemory(proc, (LPCVOID)addr, &val, sizeof(T), nullptr);
        return val;
    }

    template<typename T>
    T* as_ptr() const {
        T* val = nullptr;
        ReadProcessMemory(proc, (LPCVOID)addr, &val, sizeof(val), nullptr);
        return val;
    }
};

std::vector<Pattern> PatternList = {
    {
        "Current BPM",
        {
            { "F3 0F 11 05 ? ? ? ? F3 0F 2A 05 ? ? ? ?", 4, 6, 0, TYPE_FLOAT } // looks to be the same for every version
        }
    },
{
        "Pattern/Song Toggle",
        {
            { "48 8B 05 ? ? ? ? 83 38 ? 75 ? 48 8D 8D 90 02 00 00", 3, 6, 0, TYPE_POINTER_TO_INT }, // 24.1.2.4430
            { "48 8B 05 ? ? ? ? 83 38 ? 75 ? E8 ? ? ? ? 48 8D 88 48 1C 02 00", 3, 6, 0, TYPE_POINTER_TO_INT },

            { "48 8B 05 ? ? ? ? 83 38 ? 0F 85 ? ? ? ? B1 ?", 3, 6, 0, TYPE_POINTER_TO_INT }, // 20.6.2.1549
        }
    },
{
        "Master Pitch",
        {
		    // Credits to alessandromrc (https://github.com/alessandromrc) for finding several of these patterns

            { "48 8B 05 ? ? ? ? 03 18 48 8B 4D 20", 3, 6, 0, TYPE_POINTER_TO_SHORT },
            { "48 8B 05 ? ? ? ? F3 0F 2A 00 48 8B 45 20", 3, 6, 0, TYPE_POINTER_TO_SHORT },
            { "48 8B 05 ? ? ? ? 8B 8D B8 05 00 00 89 08 33 C0", 3, 6, 0, TYPE_POINTER_TO_SHORT },
            { "48 8B 05 ? ? ? ? 8B 00 89 85 B8 05 00 00 8B 85 C0 05 00 00 A9 ? ? ? ? 74 ? 48 8B 8D 90 03 00 00", 3, 6, 0, TYPE_POINTER_TO_SHORT },

            { "48 8B 05 ? ? ? ? 48 0F B7 00 66 89 85 08 02 00 00", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 24.2.2.4597
            { "48 8B 05 ? ? ? ? 48 8B 8D 40 0B 00 00 48 8B 89 C0 44 00 00 48 0F BF 89 2C 00 10 00", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 25.2.2.5154
			{ "48 8B 05 ? ? ? ? 48 0F B7 00 66 89 85 D8 04 00 00", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 21.0.3.3517
            { "48 8B 05 ? ? ? ? 48 8B 8D 90 09 00 00 48 8B 89 00 04 00 00 48 0F BF 89 2C 00 10 00", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 21.1.1.3750
            { "48 8B 05 ? ? ? ? 48 8B 8D C0 09 00 00 48 8B 89 20 48 00 00 48 0F BF 89 2C 00 10 00", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 24.1.2.4430
            { "48 8B 05 ? ? ? ? 48 8B 8D 50 0B 00 00 48 8B 89 50 48 00 00 48 0F BF 89 2C 00 10 00", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 25.1.1.4879
            { "48 8B 05 ? ? ? ? 48 8B 8D 40 0B 00 00 48 8B 89 40 48 00 00 48 0F BF 89 2C 00 10 00", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 25.1.5.4976

            { "48 8B 05 ? ? ? ? 48 8B 8D ? ? ? ? 48 8B 89 ? ? ? ? 48 0F BF 89", 3, 6, 0, TYPE_POINTER_TO_SHORT }, // 20.6.2.1549
        }
    },
{
        "Metronome Toggle",
        {
            { "48 8B 05 ? ? ? ? 48 0F B6 10 E8 ? ? ? ? 48 8B 05 ? ? ? ? 48 8B 00 48 8B 88 F8 0C 00 00", 3, 6, 0, TYPE_POINTER_TO_BYTE }, // 21.0.3.3517
            { "48 8B 0D ? ? ? ? 48 0F B6 09 88 88 88 09 00 00", 3, 6, 0, TYPE_POINTER_TO_BYTE }, // 24.2.2.4597

            { "48 8B 05 ? ? ? ? ? ? ? 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? ? ? 48 8B 05", 3, 6, 0, TYPE_POINTER_TO_BYTE }, // 20.6.2.1549
        }
    },
{
        "Playing Status",
        {
            { "48 8B 0D ? ? ? ? 83 39 ? 0F 94 C1 85 C0", 3, 6, 0, TYPE_POINTER_TO_INT }, // 21.0.3.3517
            { "83 3D ? ? ? ? ? 74 ? 83 3D ? ? ? ? ? 7F", 2, 7, 0, TYPE_BYTE }, // versions 24 and up

            { "48 8B 05 ? ? ? ? ? ? ? 0F 85 ? ? ? ? 48 8B 05 ? ? ? ? ? ? 48 8B 0D", 3, 6, 0, TYPE_POINTER_TO_INT }, // 20.6.2.1549

        }
    }
};

std::vector<Pattern> FeaturePatternList = {
    {
        "Song Position Transport",
        {
            { "48 8B 05 ? ? ? ? 48 8B 00 48 8B 80 E8 07 00 00 8B 88 C0 03 00 00 E8 ? ? ? ? 89 C3", 3, 6, 0, TYPE_POINTER_TO_INT }, // 25.1.6.4997, 26.1.6.5639
        }
    },
    {
        "Song Start Offset",
        {
            { "48 8B 05 ? ? ? ? 83 38 01 75 ? 48 8B 05 ? ? ? ? 2B 08 89 C8", 15, 6, 0, TYPE_POINTER_TO_INT }, // 26.1.6.5639
        }
    },
    {
        "Time Display Mode",
        {
            { "48 83 EC ? 48 8B 05 ? ? ? ? ? ? ? ? 48 8B 05 ? ? ? ? ? ? 48 83 C4 ? C3 ? ? ? 53 48 83 EC ? 48 89 CB 48 C7 43 ? ? ? ? ? 48 8B 05 ? ? ? ? ? ? ? 48 8B 40 ? 48 89 C1 ? ? ? FF 90 ? ? ? ? 84 C0 74 ? 48 C7 43 ? 00 00 00 00 E8", 7, 6, 0, TYPE_POINTER_TO_BYTE }, // 26.1.6.5639
        }
    },
    {
        "Project Contexts",
        {
            { "48 8B 05 ? ? ? ? 48 63 0D ? ? ? ? 48 8B 04 C8 C3", 3, 6, 0, TYPE_POINTER_TO_INT }, // 26.1.6.5639
        }
    },
    {
        "Current Project Index",
        {
            { "48 8B 05 ? ? ? ? 48 63 0D ? ? ? ? 48 8B 04 C8 C3", 10, 6, 0, TYPE_INT }, // 26.1.6.5639
        }
    },
    {
        "Project Name",
        {
            { "48 8D 4D ? 48 8B 05 ? ? ? ? 48 8B 10 E8 ? ? ? ? 48 8B 4D ? 48 8D 15 ? ? ? ? E8 ? ? ? ? 85 C0", 7, 6, 0, TYPE_POINTER_TO_INT }, // 25.1.6.4997, 26.1.6.5639
        }
    }
};

DWORD GetProcId(const wchar_t* name) {
    PROCESSENTRY32W pe{ sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, name)) {
                CloseHandle(snap);
                return pe.th32ProcessID;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return 0;
}

uintptr_t GetModuleBase(DWORD pid, const wchar_t* modName, DWORD& modSize) {
    MODULEENTRY32W me{ sizeof(me) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (Module32FirstW(snap, &me)) {
        do {
            if (!_wcsicmp(me.szModule, modName)) {
                modSize = me.modBaseSize;
                CloseHandle(snap);
                return reinterpret_cast<uintptr_t>(me.modBaseAddr);
            }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return 0;
}

std::vector<int> PatternToBytes(const char* pattern) {
    std::vector<int> bytes;
    char* start = const_cast<char*>(pattern);
    char* end = start + strlen(pattern);
    for (char* cur = start; cur < end; ++cur) {
        if (*cur == '?') {
            bytes.push_back(-1);
            if (*(cur + 1) == '?') ++cur;
        }
        else if (isxdigit(*cur)) {
            bytes.push_back(strtoul(cur, &cur, 16));
            --cur;
        }
    }
    return bytes;
}

uintptr_t FindPattern(HANDLE proc, uintptr_t base, DWORD size, const char* sig) {
    auto pattern = PatternToBytes(sig);
    std::vector<BYTE> buffer(size);
    if (!ReadProcessMemory(proc, (LPCVOID)base, buffer.data(), size, nullptr)) return 0;

    for (size_t i = 0; i < size - pattern.size(); i++) {
        bool found = true;
        for (size_t j = 0; j < pattern.size(); j++) {
            if (pattern[j] != -1 && buffer[i + j] != pattern[j]) {
                found = false;
                break;
            }
        }
        if (found) return base + i;
    }
    return 0;
}

uintptr_t FindFeaturePatternAddress(HANDLE proc, uintptr_t base, DWORD size, const std::string& name) {
    for (const auto& feature : FeaturePatternList) {
        if (feature.name != name) continue;
        for (const auto& signature : feature.signatures) {
            uintptr_t hit = FindPattern(proc, base, size, signature.pattern);
            if (hit) {
                return MemHandle{ proc, hit }
                    .add(signature.ripOffset)
                    .rip(signature.instrLen)
                    .add(signature.postOffset)
                    .addr;
            }
        }
    }
    return 0;
}

std::wstring GetProcessProductVersion(DWORD pid) {
    wchar_t path[MAX_PATH];
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return L"Unknown";
    DWORD size = MAX_PATH;
    QueryFullProcessImageNameW(hProc, 0, path, &size);
    CloseHandle(hProc);

    auto getSize = GetSystemVersionProc<decltype(&GetFileVersionInfoSizeW)>("GetFileVersionInfoSizeW");
    auto getInfo = GetSystemVersionProc<decltype(&GetFileVersionInfoW)>("GetFileVersionInfoW");
    auto queryValue = GetSystemVersionProc<decltype(&VerQueryValueW)>("VerQueryValueW");
    if (!getSize || !getInfo || !queryValue) return L"Unknown";

    DWORD dummy;
    DWORD verSize = getSize(path, &dummy);
    if (!verSize) return L"Unknown";
    std::vector<BYTE> data(verSize);
    if (!getInfo(path, 0, verSize, data.data())) return L"Unknown";

    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!queryValue(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &len) || !info) return L"Unknown";

    return std::to_wstring(HIWORD(info->dwProductVersionMS)) + L"." +
        std::to_wstring(LOWORD(info->dwProductVersionMS)) + L"." +
        std::to_wstring(HIWORD(info->dwProductVersionLS)) + L"." +
        std::to_wstring(LOWORD(info->dwProductVersionLS));
}

struct SongPositionAddresses {
    uintptr_t transportRoot{};
    uintptr_t songStartOffset{};
    uintptr_t timeDisplayMinutes{};
    uintptr_t projectContexts{};
    uintptr_t currentProjectIndex{};
};

SongPositionAddresses FindSongPositionAddresses(HANDLE proc, uintptr_t base, DWORD size) {
    return {
        FindFeaturePatternAddress(proc, base, size, "Song Position Transport"),
        FindFeaturePatternAddress(proc, base, size, "Song Start Offset"),
        FindFeaturePatternAddress(proc, base, size, "Time Display Mode"),
        FindFeaturePatternAddress(proc, base, size, "Project Contexts"),
        FindFeaturePatternAddress(proc, base, size, "Current Project Index")
    };
}

int ReadSongPositionTicks(HANDLE proc, const SongPositionAddresses& addresses, bool songMode) {
    uintptr_t rootVariable = 0;
    uintptr_t root = 0;
    uintptr_t transport = 0;
    int position = 0;

    if (!addresses.transportRoot ||
        !ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addresses.transportRoot), &rootVariable, sizeof(rootVariable), nullptr) ||
        !rootVariable ||
        !ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(rootVariable), &root, sizeof(root), nullptr) ||
        !root ||
        !ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(root + 0x7E8), &transport, sizeof(transport), nullptr) ||
        !transport ||
        !ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(transport + 0x3C0), &position, sizeof(position), nullptr)) {
        return 0;
    }

    if (songMode && addresses.songStartOffset) {
        uintptr_t offsetVariable = 0;
        int offset = 0;
        if (ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addresses.songStartOffset), &offsetVariable, sizeof(offsetVariable), nullptr) &&
            offsetVariable &&
            ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(offsetVariable), &offset, sizeof(offset), nullptr)) {
            position -= offset;
        }
    }

    if (addresses.projectContexts && addresses.currentProjectIndex) {
        uintptr_t contexts = 0;
        uintptr_t project = 0;
        int projectIndex = 0;
        int displayOffset = 0;
        if (ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addresses.projectContexts), &contexts, sizeof(contexts), nullptr) &&
            contexts &&
            ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addresses.currentProjectIndex), &projectIndex, sizeof(projectIndex), nullptr) &&
            projectIndex >= 0 && projectIndex < 64 &&
            ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(contexts + static_cast<uintptr_t>(projectIndex) * sizeof(uintptr_t)), &project, sizeof(project), nullptr) &&
            project &&
            ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(project + 0x21C58), &displayOffset, sizeof(displayOffset), nullptr)) {
            position += displayOffset;
        }
    }

    return position > 0 ? position : 0;
}

bool ReadTimeDisplayMinutes(HANDLE proc, const SongPositionAddresses& addresses) {
    uintptr_t displayModeVariable = 0;
    unsigned char displayMinutes = 1;
    if (addresses.timeDisplayMinutes &&
        ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(addresses.timeDisplayMinutes), &displayModeVariable, sizeof(displayModeVariable), nullptr) &&
        displayModeVariable) {
        ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(displayModeVariable), &displayMinutes, sizeof(displayMinutes), nullptr);
    }
    return displayMinutes != 0;
}

uintptr_t FindProjectNameAddress(HANDLE proc, uintptr_t base, DWORD size) {
    return FindFeaturePatternAddress(proc, base, size, "Project Name");
}

std::wstring ReadRemoteUnicodeString(HANDLE proc, uintptr_t address) {
    uintptr_t stringVariable = 0;
    uintptr_t data = 0;
    int32_t length = 0;

    if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(address), &stringVariable, sizeof(stringVariable), nullptr) ||
        !stringVariable ||
        !ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(stringVariable), &data, sizeof(data), nullptr) ||
        !data ||
        !ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(data - sizeof(length)), &length, sizeof(length), nullptr) ||
        length <= 0 || length > 4096) {
        return {};
    }

    std::wstring value(length, L'\0');
    if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(data), value.data(), length * sizeof(wchar_t), nullptr)) {
        return {};
    }
    return value;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) return {};

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string GetFLStudioProjectNameFromWindow() {
    HWND hwnd = FindWindow(L"TFruityLoopsMainForm", nullptr);
    if (!hwnd) return g_config.defaultProjectName;

    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));

    std::string windowTitle = WideToUtf8(title);
    size_t separator = windowTitle.rfind('-');
    if (separator == std::string::npos) return g_config.defaultProjectName;

    std::string projectName = windowTitle.substr(0, separator);
    size_t end = projectName.find_last_not_of(" \t");
    if (end == std::string::npos) return g_config.defaultProjectName;
    projectName.erase(end + 1);
    return projectName;
}

std::string GetFLStudioProjectName(HANDLE proc, uintptr_t address) {
    std::wstring path = ReadRemoteUnicodeString(proc, address);
    if (path.empty()) return GetFLStudioProjectNameFromWindow();

    size_t separator = path.find_last_of(L"\\/");
    std::wstring name = path.substr(separator == std::wstring::npos ? 0 : separator + 1);
    size_t extension = name.find_last_of(L'.');
    if (extension != std::wstring::npos) name.erase(extension);

    if (name.empty()) return GetFLStudioProjectNameFromWindow();
    if (!_wcsicmp(name.c_str(), L"untitled")) return g_config.defaultProjectName;
    std::string projectName = WideToUtf8(name);
    return projectName.empty() ? GetFLStudioProjectNameFromWindow() : projectName;
}


// ---------------------- Global state ----------------------
std::string g_projectName;
std::string g_playState;
float g_bpm;
int g_masterPitch;
bool g_metronome;
bool g_songMode;
std::string g_songPosition = "0:00:00";
int64_t g_startTimestamp;
std::string g_flVersion;

// ---------------------- Helpers ----------------------
std::string FormatSongPosition(int ticks, bool displayMinutes) {
    std::ostringstream result;
    if (displayMinutes) {
        if (g_bpm <= 0.0f) return "0:00:00";
        int totalMilliseconds = static_cast<int>(std::lround((ticks * 60000.0) / (g_bpm * 96.0)));
        int totalCentiseconds = totalMilliseconds / 10;
        int minutes = totalCentiseconds / 6000;
        int seconds = (totalCentiseconds / 100) % 60;
        int centiseconds = totalCentiseconds % 100;
        result << minutes << ':'
            << std::setfill('0') << std::setw(2) << seconds << ':'
            << std::setw(2) << centiseconds;
    }
    else {
        constexpr int ticksPerStep = 24;
        constexpr int stepsPerBar = 16;
        int bar = ticks / (ticksPerStep * stepsPerBar) + 1;
        int step = (ticks / ticksPerStep) % stepsPerBar + 1;
        int tick = ticks % ticksPerStep;
        result << bar << ':'
            << std::setfill('0') << std::setw(2) << step << ':'
            << std::setw(2) << tick;
    }
    return result.str();
}

std::string ReplacePlaceholders(const std::string& text) {
    std::string result = text;
    auto replace = [&](const std::string& key, const std::string& value) {
        size_t pos = 0;
        while ((pos = result.find(key, pos)) != std::string::npos) {
            result.replace(pos, key.length(), value);
            pos += value.length();
        }
        };

    replace("{Project}", g_projectName);

    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(3) << g_bpm;
        std::string bpmStr = ss.str();

        bpmStr.erase(bpmStr.find_last_not_of('0') + 1);

        if (!bpmStr.empty() && bpmStr.back() == '.') {
            bpmStr.pop_back();
        }

        replace("{BPM}", bpmStr);
    }

    replace("{Pitch}", std::to_string(g_masterPitch));
    replace("{Metronome}", g_metronome ? "ON" : "OFF");
    replace("{Mode}", g_songMode ? "Song" : "Pattern");
    replace("{Status}", g_playState);
    replace("{Position}", g_songPosition);

    return result;
}

// ---------------------- Discord RPC ----------------------
void InitDiscordRPC() {
    DiscordEventHandlers handlers{};
    Discord_Initialize(g_config.discordAppId.c_str(), &handlers, 1, nullptr);
    g_startTimestamp = time(nullptr);
}

void ShutdownDiscordRPC() {
    Discord_Shutdown();
}

void UpdateDiscordRPC() {
    std::string detailsStr = ReplacePlaceholders(g_config.presenceDetails);
    std::string stateStr = ReplacePlaceholders(g_config.presenceState);

    DiscordRichPresence rpc{};
    rpc.startTimestamp = g_startTimestamp;
    rpc.largeImageKey = g_config.largeImageKey.c_str();
    rpc.largeImageText = g_flVersion.c_str();
    rpc.details = detailsStr.c_str();
    rpc.state = stateStr.c_str();
    rpc.smallImageKey = (g_playState == "Playing") ? g_config.playingImageKey.c_str() : g_config.stoppedImageKey.c_str();

    Discord_UpdatePresence(&rpc);
    std::cout << "[Discord Update] " << rpc.details << " | " << rpc.state << std::endl;
}

// ---------------------- Main ----------------------
void RunTracker(const std::string& configPath = "config.json") {
    if (!LoadConfig(configPath, g_config))
        std::cout << "Failed to load " << configPath << ", using defaults.\n";

    const wchar_t* PROCESS_NAME = L"FL64.exe";
    const wchar_t* MODULE_NAME = L"FLEngine_x64.dll";

    std::cout << "Waiting for FL Studio...\n";

    while (true) {
        DWORD pid = 0;
        while ((pid = GetProcId(PROCESS_NAME)) == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        std::cout << "FL Studio found. Attaching...\n";

        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) {
            std::cout << "Failed to open process. Retrying...\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        DWORD modSize = 0;
        uintptr_t modBase = GetModuleBase(pid, MODULE_NAME, modSize);
        if (!modBase) {
            std::cout << "Failed to find FLEngine_x64.dll. Retrying...\n";
            CloseHandle(hProc);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        std::wstring wVer = GetProcessProductVersion(pid);
        g_flVersion = "FL Studio v" + WideToUtf8(wVer);

        SongPositionAddresses songPositionAddresses = FindSongPositionAddresses(hProc, modBase, modSize);
        if (songPositionAddresses.transportRoot) {
            std::cout << "[Memory] Pattern: Song Position"
                << " | Address: 0x" << std::hex << songPositionAddresses.transportRoot
                << " | Module+Offset: 0x" << (songPositionAddresses.transportRoot - modBase)
                << std::dec << "\n";
        }
        else {
            std::cout << "WARNING: Pattern 'Song Position' was not found. Position will remain at 0:00:00.\n";
        }
        if (!songPositionAddresses.timeDisplayMinutes) {
            std::cout << "WARNING: Pattern 'Time Display Mode' was not found. Position will use M:S:CS.\n";
        }

        uintptr_t projectNameAddress = FindProjectNameAddress(hProc, modBase, modSize);
        if (projectNameAddress) {
            std::cout << "[Memory] Pattern: Project Name"
                << " | Address: 0x" << std::hex << projectNameAddress
                << " | Module+Offset: 0x" << (projectNameAddress - modBase)
                << std::dec << "\n";
        }
        else {
            std::cout << "WARNING: Pattern 'Project Name' was not found. Falling back to the window title.\n";
        }

        g_projectName = GetFLStudioProjectName(hProc, projectNameAddress);
        InitDiscordRPC();

        std::wcout << L"--- Target: " << PROCESS_NAME << L" (Version: " << GetProcessProductVersion(pid) << L") ---\n\n";

        std::unordered_map<std::string, bool> printedPatterns;
        bool initialCheckDone = false;

        while (GetProcId(PROCESS_NAME) != 0) {
            for (const auto& p : PatternList) {
                uintptr_t hit = 0;
                const Signature* matchedSig = nullptr;

                for (const auto& sig : p.signatures) {
                    hit = FindPattern(hProc, modBase, modSize, sig.pattern);
                    if (hit) {
                        matchedSig = &sig;
                        break;
                    }
                }
                if (!hit || !matchedSig) continue;

                MemHandle ptr{ hProc, hit };
                MemHandle resolved =
                    ptr.add(matchedSig->ripOffset)
                    .rip(matchedSig->instrLen)
                    .add(matchedSig->postOffset);

                if (!printedPatterns[p.name]) {
                    std::cout << "[Memory] Pattern: " << p.name
                        << " | Address: 0x" << std::hex << resolved.addr
                        << " | Module+Offset: 0x" << (resolved.addr - modBase)
                        << std::dec << "\n";
                    printedPatterns[p.name] = true;
                }

                if (p.name == "Current BPM") {
                    g_bpm = resolved.as<float>();
                }
                else if (p.name == "Pattern/Song Toggle") {
                    int val = 0;
                    if (matchedSig->type == TYPE_POINTER_TO_INT) {
                        auto ptrVal = resolved.as_ptr<int>();
                        if (ptrVal) ReadProcessMemory(hProc, ptrVal, &val, sizeof(val), nullptr);
                    }
                    g_songMode = (val == 1);
                }
                else if (p.name == "Master Pitch") {
                    int16_t raw = 0;
                    if (matchedSig->type == TYPE_POINTER_TO_SHORT) {
                        auto ptrVal = resolved.as_ptr<int16_t>();
                        if (ptrVal) ReadProcessMemory(hProc, ptrVal, &raw, sizeof(raw), nullptr);
                    }
                    g_masterPitch = static_cast<int>(raw);
                }
                else if (p.name == "Metronome Toggle") {
                    unsigned char val = 0;
                    if (matchedSig->type == TYPE_POINTER_TO_BYTE) {
                        auto ptrVal = resolved.as_ptr<unsigned char>();
                        if (ptrVal) ReadProcessMemory(hProc, ptrVal, &val, sizeof(val), nullptr);
                    }
                    g_metronome = (val != 0);
                }
                else if (p.name == "Playing Status") {
                    uint8_t val = 0;
                    if (matchedSig->type == TYPE_BYTE) {
                        val = resolved.as<uint8_t>();
                    }
                    else if (matchedSig->type == TYPE_POINTER_TO_INT) {
                        int* ptrVal = resolved.as_ptr<int>();
                        if (ptrVal) ReadProcessMemory(hProc, ptrVal, &val, sizeof(val), nullptr);
                    }
                    g_playState = (val == 1) ? "Playing" : "Stopped";
                }
            }

            if (!initialCheckDone) {
                for (const auto& p : PatternList) {
                    if (!printedPatterns[p.name]) {
                        std::cout << "WARNING: Pattern '" << p.name
                            << "' was not found. Some features may not work.\n";
                    }
                }
                initialCheckDone = true;
            }

            g_projectName = GetFLStudioProjectName(hProc, projectNameAddress);
            if (songPositionAddresses.transportRoot) {
                g_songPosition = FormatSongPosition(
                    ReadSongPositionTicks(hProc, songPositionAddresses, g_songMode),
                    ReadTimeDisplayMinutes(hProc, songPositionAddresses)
                );
            }

            UpdateDiscordRPC();
            Discord_RunCallbacks();

            std::this_thread::sleep_for(
                std::chrono::seconds(g_config.refreshInterval)
            );
        }

        std::cout << "FL Studio closed. Waiting for relaunch...\n";
        ShutdownDiscordRPC();
        CloseHandle(hProc);
    }
}

#ifndef FLDRPC_PROXY
int main() {
    RunTracker();
    return 0;
}
#endif
