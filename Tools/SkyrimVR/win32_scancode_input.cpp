#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr WORD kEndScanCode = 0x4F;
constexpr WORD kEnterScanCode = 0x1C;
constexpr DWORD kKeyCadenceMilliseconds = 700;
constexpr wchar_t kSkyrimVrWindowTitle[] = L"Skyrim VR";
constexpr int kSkyrimVrWindowTitleLength =
    static_cast<int>(sizeof(kSkyrimVrWindowTitle) / sizeof(kSkyrimVrWindowTitle[0])) - 1;
constexpr DWORD kWindowStateTimeoutMilliseconds = 3000;
constexpr DWORD kWindowStatePollMilliseconds = 50;

struct WindowSearch {
    HWND target = nullptr;
    bool ambiguous = false;
};

BOOL CALLBACK FindExactSkyrimVrWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    if (GetAncestor(window, GA_ROOT) != window ||
        GetWindowTextLengthW(window) != kSkyrimVrWindowTitleLength) {
        return TRUE;
    }

    wchar_t title[kSkyrimVrWindowTitleLength + 1]{};
    if (GetWindowTextW(window, title, kSkyrimVrWindowTitleLength + 1) !=
            kSkyrimVrWindowTitleLength ||
        std::wcscmp(title, kSkyrimVrWindowTitle) != 0) {
        return TRUE;
    }

    if (search->target != nullptr && search->target != window) {
        search->ambiguous = true;
        return FALSE;
    }
    search->target = window;
    return TRUE;
}

HWND FindSkyrimVrWindow() {
    WindowSearch search{};
    EnumWindows(FindExactSkyrimVrWindow, reinterpret_cast<LPARAM>(&search));
    if (search.ambiguous) {
        std::fputs("multiple exact top-level Skyrim VR windows found; refusing to send input\n", stderr);
        return nullptr;
    }
    if (search.target == nullptr) {
        std::fputs("exact top-level Skyrim VR window was not found; refusing to send input\n", stderr);
    }
    return search.target;
}

bool RestoreIfMinimized(HWND target) {
    if (!IsIconic(target)) {
        return true;
    }

    ShowWindowAsync(target, SW_RESTORE);
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMilliseconds;
    while (IsIconic(target)) {
        if (!IsWindow(target) || GetTickCount64() >= deadline) {
            std::fputs("Skyrim VR window remained minimized; refusing to send input\n", stderr);
            return false;
        }
        Sleep(kWindowStatePollMilliseconds);
    }
    return true;
}

bool WaitForForeground(HWND target) {
    const ULONGLONG deadline = GetTickCount64() + kWindowStateTimeoutMilliseconds;
    do {
        if (!IsWindow(target)) {
            std::fputs("Skyrim VR window closed while activating it; refusing to send input\n", stderr);
            return false;
        }
        if (GetForegroundWindow() == target) {
            return true;
        }
        Sleep(kWindowStatePollMilliseconds);
    } while (GetTickCount64() < deadline);

    std::fputs("Skyrim VR window did not become foreground; refusing to send input\n", stderr);
    return false;
}

class ThreadInputAttachment {
public:
    ThreadInputAttachment() = default;
    ThreadInputAttachment(const ThreadInputAttachment&) = delete;
    ThreadInputAttachment& operator=(const ThreadInputAttachment&) = delete;

    ~ThreadInputAttachment() {
        Detach();
    }

    bool Attach(DWORD firstThread, DWORD secondThread, const char* description) {
        if (firstThread == 0 || secondThread == 0) {
            std::fprintf(stderr, "could not identify %s input thread; refusing to send input\n", description);
            return false;
        }
        if (firstThread == secondThread) {
            return true;
        }
        if (!AttachThreadInput(firstThread, secondThread, TRUE)) {
            std::fprintf(stderr, "could not attach %s input threads: %lu\n", description, GetLastError());
            return false;
        }
        firstThread_ = firstThread;
        secondThread_ = secondThread;
        attached_ = true;
        return true;
    }

    bool Detach() {
        if (!attached_) {
            return true;
        }
        if (!AttachThreadInput(firstThread_, secondThread_, FALSE)) {
            std::fprintf(stderr, "could not detach Win32 input threads: %lu\n", GetLastError());
            return false;
        }
        attached_ = false;
        return true;
    }

private:
    DWORD firstThread_ = 0;
    DWORD secondThread_ = 0;
    bool attached_ = false;
};

void RequestForeground(HWND target) {
    BringWindowToTop(target);
    SetForegroundWindow(target);
}

bool ActivateSkyrimVrWindow(HWND target) {
    if (!RestoreIfMinimized(target)) {
        return false;
    }

    RequestForeground(target);
    if (WaitForForeground(target)) {
        return true;
    }

    // A foreground-lock restriction can require joining the current, target,
    // and foreground input queues before retrying the activation request.
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    const DWORD helperThread = GetCurrentThreadId();
    const DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread =
        foreground == nullptr ? 0 : GetWindowThreadProcessId(foreground, nullptr);
    ThreadInputAttachment targetInput;
    ThreadInputAttachment foregroundInput;
    if (!targetInput.Attach(helperThread, targetThread, "helper and Skyrim VR")) {
        return false;
    }
    if (foreground != nullptr && foreground != target && foregroundThread != targetThread &&
        !foregroundInput.Attach(helperThread, foregroundThread, "helper and foreground")) {
        return false;
    }

    RequestForeground(target);
    SetActiveWindow(target);
    SetFocus(target);
    const bool foregroundVerified = WaitForForeground(target);
    const bool foregroundDetached = foregroundInput.Detach();
    const bool targetDetached = targetInput.Detach();
    return foregroundVerified && foregroundDetached && targetDetached;
}

bool SendScanCode(HWND target, WORD scanCode, bool extended, bool keyUp) {
    if (GetForegroundWindow() != target) {
        std::fputs("Skyrim VR window lost foreground; refusing to send input\n", stderr);
        return false;
    }

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = scanCode;
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (extended) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    if (keyUp) {
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    }

    if (SendInput(1, &input, sizeof(input)) == 1) {
        return true;
    }

    std::fprintf(stderr, "SendInput failed for scan code 0x%02X (%s): %lu\n",
        scanCode, keyUp ? "up" : "down", GetLastError());
    return false;
}

bool TapScanCode(HWND target, WORD scanCode, bool extended) {
    if (!SendScanCode(target, scanCode, extended, false)) {
        return false;
    }
    Sleep(kKeyCadenceMilliseconds);
    if (!SendScanCode(target, scanCode, extended, true)) {
        return false;
    }
    Sleep(kKeyCadenceMilliseconds);
    return true;
}

void PrintUsage(const char* program) {
    std::fprintf(stderr, "Usage: %s --end-enter | --check\n", program);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        PrintUsage(argv[0]);
        return 2;
    }
    if (std::strcmp(argv[1], "--check") == 0) {
        std::puts("win32_scancode_input: ready (no input sent)");
        return 0;
    }
    if (std::strcmp(argv[1], "--end-enter") != 0) {
        PrintUsage(argv[0]);
        return 2;
    }

    const HWND target = FindSkyrimVrWindow();
    if (target == nullptr || !ActivateSkyrimVrWindow(target) ||
        GetForegroundWindow() != target) {
        return 1;
    }

    // Skyrim VR's Main Menu is indexed bottom-to-top: End selects New and
    // Enter activates it. End is an extended scan code; Enter is not.
    return TapScanCode(target, kEndScanCode, true) &&
            TapScanCode(target, kEnterScanCode, false)
        ? 0
        : 1;
}
