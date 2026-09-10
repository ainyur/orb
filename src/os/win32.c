#include "../core/log.h"
#include "os.h"
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <shellapi.h> // after windows.h

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef wchar_t win32_wpath[ORB_PATH_MAX];

static int win32_compare_names(const void* a, const void* b) {
    return strcmp(a, b);
}

static uint64_t win32_filetime_ns(FILETIME t) {
    ULARGE_INTEGER u = {.LowPart = t.dwLowDateTime, .HighPart = t.dwHighDateTime};

    return (uint64_t)u.QuadPart * 100u; // 100 ns ticks since 1601
}

// UTF-8 to UTF-16 for the W APIs. A string that does not fit becomes empty, so
// the call fails on no name rather than succeeding on a truncated one.
static const wchar_t* win32_wide(const char* utf8, wchar_t* out, int cap) {
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap)) out[0] = 0;

    return out;
}

// UTF-16 back to UTF-8; the length including the NUL, or 0 when it does not fit.
static int win32_narrow(const wchar_t* wide, char* out, int cap) {
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, cap, nullptr, nullptr);
}

void orb_os_args(int* argc, char*** argv) {
    static char* args[65]; // 64 and the NULL after them
    static char text[4096];
    int n;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &n);

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

bool orb_os_copy_file(const char* from, const char* to) {
    win32_wpath f, t;

    return CopyFileW(win32_wide(from, f, ORB_PATH_MAX), win32_wide(to, t, ORB_PATH_MAX), FALSE) !=
           0;
}

void* orb_os_dlopen(const char* path) {
    win32_wpath w;

    return LoadLibraryW(win32_wide(path, w, ORB_PATH_MAX));
}

void orb_os_dlclose(void* lib) {
    FreeLibrary(lib);
}

void* orb_os_dlsym(void* lib, const char* name) {
    return (void*)GetProcAddress(lib, name);
}

uint64_t orb_os_file_mtime(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    win32_wpath w;

    if (!GetFileAttributesExW(win32_wide(path, w, ORB_PATH_MAX), GetFileExInfoStandard, &info))
        return 0;

    return win32_filetime_ns(info.ftLastWriteTime);
}

int orb_os_list_dir(const char* dir, const char* suffix, orb_path* out, int max) {
    orb_path pattern;

    orb_path_join(pattern, dir, "*");

    WIN32_FIND_DATAW found;
    win32_wpath w;
    HANDLE h = FindFirstFileW(win32_wide(pattern, w, ORB_PATH_MAX), &found);

    if (h == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    size_t suffix_len = strlen(suffix);

    do {
        orb_path name; // 255 characters can be 765 bytes; past a path's worth is skipped

        if (!win32_narrow(found.cFileName, name, sizeof name)) continue;

        size_t n = strlen(name);

        if (n < suffix_len || strcmp(name + n - suffix_len, suffix) != 0) continue;

        orb_path_join(out[count], dir, name);

        if (orb_os_file_mtime(out[count]) != 0) count++;
    } while (count < max && FindNextFileW(h, &found));

    FindClose(h);
    qsort(out, (size_t)count, sizeof out[0], win32_compare_names);

    return count;
}

bool orb_os_make_dir(const char* path) {
    win32_wpath w;

    return CreateDirectoryW(win32_wide(path, w, ORB_PATH_MAX), nullptr) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

bool orb_os_read_file(const char* path, orb_arena* into, orb_span* out) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    win32_wpath w;

    if (!GetFileAttributesExW(win32_wide(path, w, ORB_PATH_MAX), GetFileExInfoStandard, &info))
        return false;

    size_t size = ((size_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;

    // push before opening: an exhausted arena may longjmp out of here
    uint8_t* data = orb_arena_push(into, size + 1, 16);

    FILE* f = _wfopen(w, L"rb");

    if (!f) return false;

    out->len = fread(data, 1, size, f);
    data[out->len] = 0;
    out->ptr = data;

    fclose(f);

    return out->len == size;
}

// Runs through cmd.exe with stdout and stderr on one pipe, so the caller sees
// the same combined stream popen's "2>&1" gives on POSIX.
int orb_os_run(const char* command, void (*line)(const char* text)) {
    SECURITY_ATTRIBUTES inherit = {.nLength = sizeof inherit, .bInheritHandle = TRUE};
    HANDLE read_end, write_end;

    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) return -1;

    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    char utf8[1100];
    wchar_t cmdline[1100];

    snprintf(utf8, sizeof utf8, "cmd.exe /c %s", command);
    win32_wide(utf8, cmdline, 1100);

    STARTUPINFOW start = {
        .cb = sizeof start,
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdOutput = write_end,
        .hStdError = write_end,
    };
    PROCESS_INFORMATION proc;
    BOOL started = CreateProcessW(
        nullptr, cmdline, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &proc
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

// A high-resolution waitable timer (Windows 10 1803) sleeps to within tens of
// microseconds; Sleep rounds to the scheduler tick, up to 16 ms, which would
// jitter every frame at 60 Hz.
void orb_os_sleep(uint64_t ns) {
    static HANDLE timer;

    if (!timer) {
        timer = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS
        );
    }

    if (!timer) timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);

    if (!timer) {
        Sleep((DWORD)((ns + 999999u) / 1000000u));
        return;
    }

    LARGE_INTEGER due = {.QuadPart = -(LONGLONG)((ns + 99u) / 100u)}; // relative, 100 ns units

    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
}

uint64_t orb_os_ticks(void) {
    LARGE_INTEGER count, freq;

    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&freq);

    uint64_t c = (uint64_t)count.QuadPart, f = (uint64_t)freq.QuadPart;

    return c / f * 1000000000u + c % f * 1000000000u / f;
}

bool orb_os_write_file(const char* path, orb_span data) {
    win32_wpath w;
    FILE* f = _wfopen(win32_wide(path, w, ORB_PATH_MAX), L"wb");

    if (!f) return false;

    bool ok = fwrite(data.ptr, 1, data.len, f) == data.len;

    return fclose(f) == 0 && ok;
}

void orb_path_join(orb_path out, const char* dir, const char* rel) {
    bool absolute = rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':');
    int n = absolute ? snprintf(out, ORB_PATH_MAX, "%s", rel)
                     : snprintf(out, ORB_PATH_MAX, "%s/%s", dir, rel);

    if (n >= ORB_PATH_MAX) orb_fatal("path too long: %s/%s", dir, rel);
}
