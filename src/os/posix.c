#include "../core/log.h"
#include "os.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

void orb_os_args(int*, char***) {
}

bool orb_os_copy_file(const char* from, const char* to) {
    FILE* in = fopen(from, "rb");

    if (!in) return false;

    FILE* out = fopen(to, "wb");

    if (!out) {
        fclose(in);
        return false;
    }

    uint8_t chunk[1 << 16];
    bool ok = true;

    for (size_t n; ok && (n = fread(chunk, 1, sizeof chunk, in)) > 0;)
        ok = fwrite(chunk, 1, n, out) == n;

    ok = ok && !ferror(in);

    fclose(in);

    if (fclose(out) != 0) ok = false;
    if (!ok) remove(to);

    return ok;
}

void* orb_os_dlopen(const char* path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void orb_os_dlclose(void* lib) {
    dlclose(lib);
}

void* orb_os_dlsym(void* lib, const char* name) {
    return dlsym(lib, name);
}

FILE* orb_os_fopen(const char* path, const char* mode) {
    return fopen(path, mode);
}

bool orb_os_make_dir(const char* path) {
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

// Strict POSIX hides d_type, so each entry is stat'd; one that fails is skipped.
int orb_os_read_dir(const char* dir, orb_os_entry* out, int max) {
    DIR* d = opendir(dir);

    if (!d) return 0;

    int count = 0;

    for (struct dirent* e; (e = readdir(d)) && count < max;) {
        orb_path path;
        orb_os_info info;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;

        orb_path_join(path, dir, e->d_name);

        if (!orb_os_stat(path, &info)) continue;

        snprintf(out[count].name, ORB_PATH_MAX, "%s", e->d_name);
        out[count++].dir = info.dir;
    }

    closedir(d);
    return count;
}

int orb_os_run(const char* command, void (*line)(const char* text)) {
    char piped[1024];
    snprintf(piped, sizeof piped, "%s 2>&1", command);
    FILE* p = popen(piped, "r");

    if (!p) return -1;

    char text[1024];

    while (fgets(text, sizeof text, p)) {
        text[strcspn(text, "\n")] = 0;
        line(text);
    }

    int status = pclose(p);

    return status == -1 ? -1 : WEXITSTATUS(status);
}

void orb_os_sleep(uint64_t ns) {
    struct timespec ts = {
        .tv_sec = (time_t)(ns / 1000000000u), .tv_nsec = (long)(ns % 1000000000u)
    };

    nanosleep(&ts, nullptr);
}

uint64_t orb_os_ticks(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

bool orb_os_stat(const char* path, orb_os_info* out) {
    struct stat st;

    if (stat(path, &st) != 0) return false;

    *out = (orb_os_info) {
        .size = (uint64_t)st.st_size,
        .mtime = (uint64_t)st.st_mtim.tv_sec * 1000000000u + (uint64_t)st.st_mtim.tv_nsec,
        .dir = S_ISDIR(st.st_mode)
    };
    return true;
}

#include "stdio.c"
