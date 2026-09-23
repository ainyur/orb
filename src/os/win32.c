#include "../core/log.h"
#include "os.h"
#define UNICODE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <shellapi.h> // after windows.h

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef wchar_t win32_wpath[ORB_PATH_MAX];

// UTF-8 to UTF-16. A string that does not fit becomes empty, so the call fails
// rather than acting on a truncated name.
static const wchar_t* win32_wide(const char* utf8, wchar_t* out, int cap) {
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap)) out[0] = 0;

    return out;
}

// UTF-16 back to UTF-8; the length including the NUL, or 0 when it does not fit.
static int win32_narrow(const wchar_t* wide, char* out, int cap) {
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, cap, nullptr, nullptr);
}

static uint64_t win32_filetime_ns(FILETIME filetime) {
    ULARGE_INTEGER value = {.LowPart = filetime.dwLowDateTime, .HighPart = filetime.dwHighDateTime};

    return (uint64_t)value.QuadPart * 100u; // 100 ns ticks since 1601
}

void orb_os_args(int* argc, char*** argv) {
    static char* args[65];
    static char text[4096];
    int n;
    wchar_t** wide = CommandLineToArgvW(GetCommandLine(), &n);

    if (!wide) return;
    if (n > 64) orb_fatal("too many arguments");

    size_t used = 0;

    for (int i = 0; i < n; i++) {
        int cap = (int)(sizeof text - used); // a cap of 0 would ask for the size, not fail
        int len = cap > 0 ? win32_narrow(wide[i], text + used, cap) : 0;

        if (len == 0) orb_fatal("command line too long");

        args[i] = text + used;
        used += (size_t)len;
    }

    args[n] = nullptr;
    LocalFree(wide);
    *argc = n;
    *argv = args;
}

bool orb_os_stat(const char* path, orb_os_info* out) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    win32_wpath wide;

    if (!GetFileAttributesEx(win32_wide(path, wide, ORB_PATH_MAX), GetFileExInfoStandard, &info))
        return false;

    *out = (orb_os_info) {
        .size = (uint64_t)info.nFileSizeHigh << 32 | info.nFileSizeLow,
        .mtime = win32_filetime_ns(info.ftLastWriteTime),
        .dir = info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
    };
    return true;
}

FILE* orb_os_fopen(const char* path, const char* mode) {
    win32_wpath wide;

    return _wfopen(win32_wide(path, wide, ORB_PATH_MAX), mode[0] == 'r' ? L"rb" : L"wb");
}

bool orb_os_copy_file(const char* from, const char* to) {
    win32_wpath from_wide, to_wide;

    return CopyFile(
               win32_wide(from, from_wide, ORB_PATH_MAX), win32_wide(to, to_wide, ORB_PATH_MAX),
               FALSE
           ) != 0;
}

bool orb_os_make_dir(const char* path) {
    win32_wpath wide;

    return CreateDirectory(win32_wide(path, wide, ORB_PATH_MAX), nullptr) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

int orb_os_read_dir(const char* dir, orb_os_entry* out, int max) {
    orb_path pattern;

    orb_path_join(pattern, dir, "*");

    WIN32_FIND_DATA found;
    win32_wpath wide;
    HANDLE handle = FindFirstFile(win32_wide(pattern, wide, ORB_PATH_MAX), &found);

    if (handle == INVALID_HANDLE_VALUE) return 0;

    int count = 0;

    do {
        orb_os_entry* entry = &out[count];

        // 255 characters can be 765 bytes; a name past a path's worth is skipped.
        if (!win32_narrow(found.cFileName, entry->name, sizeof entry->name)) continue;
        if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) continue;

        entry->dir = found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        count++;
    } while (count < max && FindNextFile(handle, &found));

    FindClose(handle);
    return count;
}

orb_os_library* orb_os_dlopen(const char* path) {
    win32_wpath wide;

    return (orb_os_library*)LoadLibrary(win32_wide(path, wide, ORB_PATH_MAX));
}

void orb_os_dlclose(orb_os_library* lib) {
    FreeLibrary((HMODULE)lib);
}

void* orb_os_dlsym(orb_os_library* lib, const char* name) {
    return (void*)GetProcAddress((HMODULE)lib, name);
}

// A high-resolution waitable timer (Windows 10 1803) sleeps to within tens of
// microseconds; Sleep rounds to the scheduler tick, up to 16 ms, which would
// jitter every frame at 60 Hz.
void orb_os_sleep(uint64_t duration_ns) {
    static HANDLE timer;

    if (!timer) {
        timer = CreateWaitableTimerEx(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS
        );
    }

    if (!timer) timer = CreateWaitableTimerEx(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    if (!timer) {
        Sleep((DWORD)((duration_ns + 999999u) / 1000000u));
        return;
    }

    // relative, 100 ns units
    LARGE_INTEGER due = {.QuadPart = -(LONGLONG)((duration_ns + 99u) / 100u)};

    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
}

uint64_t orb_os_ticks(void) {
    static uint64_t ticks_per_second;
    LARGE_INTEGER count;

    if (!ticks_per_second) {
        LARGE_INTEGER freq;

        QueryPerformanceFrequency(&freq);
        ticks_per_second = (uint64_t)freq.QuadPart;
    }

    QueryPerformanceCounter(&count);

    uint64_t raw = (uint64_t)count.QuadPart;

    return raw / ticks_per_second * ORB_NS_PER_SECOND +
           raw % ticks_per_second * ORB_NS_PER_SECOND / ticks_per_second;
}

uint32_t orb_os_pid(void) {
    return GetCurrentProcessId();
}

void* orb_os_reserve(size_t size) {
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool orb_os_commit(void* at, size_t size) {
    return VirtualAlloc(at, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
}

void orb_os_release(void* base, size_t) {
    VirtualFree(base, 0, MEM_RELEASE);
}

int orb_os_run(const char* dir, const char* const* argv, void (*line)(const char* text)) {
    SECURITY_ATTRIBUTES inherit = {.nLength = sizeof inherit, .bInheritHandle = TRUE};
    HANDLE read_end, write_end;

    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) return -1;

    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    char utf8[1100];
    wchar_t cmdline[1100];
    win32_wpath dir_wide;
    int at = snprintf(utf8, sizeof utf8, "cmd.exe /c");

    for (int i = 0; argv[i] && at < (int)sizeof utf8; i++)
        at += snprintf(utf8 + at, sizeof utf8 - (size_t)at, " %s", argv[i]);

    if (at >= (int)sizeof utf8) {
        CloseHandle(read_end);
        CloseHandle(write_end);
        return -1;
    }

    win32_wide(utf8, cmdline, 1100);

    STARTUPINFO start = {
        .cb = sizeof start,
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdOutput = write_end,
        .hStdError = write_end,
    };
    PROCESS_INFORMATION proc;
    BOOL started = CreateProcess(
        nullptr, cmdline, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        dir && *dir ? win32_wide(dir, dir_wide, ORB_PATH_MAX) : nullptr, &start, &proc
    );

    CloseHandle(write_end);

    if (!started) {
        CloseHandle(read_end);
        return -1;
    }

    char text[1024];
    size_t fill = 0;
    char chunk[1024];

    for (DWORD got; ReadFile(read_end, chunk, sizeof chunk, &got, nullptr) && got > 0;) {
        for (DWORD i = 0; i < got; i++) {
            if (chunk[i] == '\n' || fill == sizeof text - 1) {
                if (fill > 0 && text[fill - 1] == '\r') fill--;

                text[fill] = 0;
                line(text);
                fill = 0;

                if (chunk[i] == '\n') continue;
            }

            text[fill++] = chunk[i];
        }
    }

    if (fill > 0) {
        text[fill] = 0;
        line(text);
    }

    CloseHandle(read_end);
    WaitForSingleObject(proc.hProcess, INFINITE);

    DWORD status;
    BOOL have_status = GetExitCodeProcess(proc.hProcess, &status);

    CloseHandle(proc.hProcess);
    CloseHandle(proc.hThread);

    return have_status ? (int)status : -1;
}

#include "stdio.c"
