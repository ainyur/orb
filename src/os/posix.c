#include "../core/log.h"
#include "orb_os.h"
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

static int posix_compare_names(const void* a, const void* b) {
    return strcmp(a, b);
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

uint64_t orb_os_file_mtime(const char* path) {
    struct stat st;

    if (stat(path, &st) != 0) return 0;

    return (uint64_t)st.st_mtim.tv_sec * 1000000000u + (uint64_t)st.st_mtim.tv_nsec;
}

int orb_os_list_dir(const char* dir, const char* suffix, orb_path* out, int max) {
    DIR* d = opendir(dir);

    if (!d) return 0;

    int count = 0;
    size_t suffix_len = strlen(suffix);

    for (struct dirent* e; (e = readdir(d)) && count < max;) {
        size_t n = strlen(e->d_name);

        if (n < suffix_len || strcmp(e->d_name + n - suffix_len, suffix) != 0) continue;

        snprintf(out[count], sizeof out[count], "%s/%s", dir, e->d_name);

        if (orb_os_file_mtime(out[count]) != 0) count++;
    }

    closedir(d);
    qsort(out, (size_t)count, sizeof out[0], posix_compare_names);
    return count;
}

bool orb_os_make_dir(const char* path) {
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

bool orb_os_read_file(const char* path, orb_arena* into, orb_span* out) {
    struct stat st;

    if (stat(path, &st) != 0) return false;

    // push before opening: an exhausted arena may longjmp out of here
    out->ptr = orb_arena_push(into, (size_t)st.st_size + 1, 16);

    FILE* f = fopen(path, "rb");

    if (!f) return false;

    out->len = fread(out->ptr, 1, (size_t)st.st_size, f);
    out->ptr[out->len] = 0;

    fclose(f);
    return out->len == (size_t)st.st_size;
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

bool orb_os_write_file(const char* path, orb_span data) {
    FILE* f = fopen(path, "wb");

    if (!f) return false;

    bool ok = fwrite(data.ptr, 1, data.len, f) == data.len;

    return fclose(f) == 0 && ok;
}

void orb_path_join(orb_path out, const char* dir, const char* rel) {
    if (snprintf(out, ORB_PATH_MAX, "%s/%s", dir, rel) >= ORB_PATH_MAX)
        orb_fatal("path too long: %s/%s", dir, rel);
}
