#include "../core/log.h"
#include "os.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void orb_os_args(int*, char***) {
}

bool orb_os_stat(const char* path, orb_os_info* out) {
    struct stat stat_buf;

    if (stat(path, &stat_buf) != 0) return false;

    *out = (orb_os_info) {
        .size = (uint64_t)stat_buf.st_size,
        .mtime = (uint64_t)stat_buf.st_mtim.tv_sec * ORB_NS_PER_SECOND +
                 (uint64_t)stat_buf.st_mtim.tv_nsec,
        .dir = S_ISDIR(stat_buf.st_mode)
    };
    return true;
}

FILE* orb_os_fopen(const char* path, const char* mode) {
    return fopen(path, mode);
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

bool orb_os_make_dir(const char* path) {
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

// Strict POSIX hides d_type, so each entry is stat'd; one that fails is skipped.
int orb_os_read_dir(const char* dir, orb_os_entry* out, int max) {
    DIR* dir_handle = opendir(dir);

    if (!dir_handle) return 0;

    int count = 0;

    for (struct dirent* entry; (entry = readdir(dir_handle)) && count < max;) {
        orb_path path;
        orb_os_info info;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        orb_path_join(path, dir, entry->d_name);

        if (!orb_os_stat(path, &info)) continue;

        snprintf(out[count].name, ORB_PATH_MAX, "%s", entry->d_name);
        out[count++].dir = info.dir;
    }

    closedir(dir_handle);
    return count;
}

orb_os_library* orb_os_dlopen(const char* path) {
    return (orb_os_library*)dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void orb_os_dlclose(orb_os_library* lib) {
    dlclose(lib);
}

void* orb_os_dlsym(orb_os_library* lib, const char* name) {
    return dlsym(lib, name);
}

void orb_os_sleep(uint64_t duration_ns) {
    struct timespec spec = {
        .tv_sec = (time_t)(duration_ns / ORB_NS_PER_SECOND),
        .tv_nsec = (long)(duration_ns % ORB_NS_PER_SECOND)
    };

    nanosleep(&spec, nullptr);
}

uint64_t orb_os_ticks(void) {
    struct timespec spec;

    clock_gettime(CLOCK_MONOTONIC, &spec);

    return (uint64_t)spec.tv_sec * ORB_NS_PER_SECOND + (uint64_t)spec.tv_nsec;
}

uint32_t orb_os_pid(void) {
    return (uint32_t)getpid();
}

void* orb_os_reserve(size_t size) {
    void* mem = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

    return mem == MAP_FAILED ? nullptr : mem;
}

bool orb_os_commit(void* at, size_t size) {
    return mprotect(at, size, PROT_READ | PROT_WRITE) == 0;
}

void orb_os_release(void* base, size_t size) {
    munmap(base, size);
}

int orb_os_run(const char* dir, const char* const* argv, void (*line)(const char* text)) {
    int fds[2];

    if (pipe(fds) != 0) return -1;

    pid_t pid = fork();

    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }

    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);

        if (dir && *dir && chdir(dir) != 0) _exit(127);

        execvp(argv[0], (char* const*)argv);
        _exit(127);
    }

    close(fds[1]);

    FILE* stream = fdopen(fds[0], "r");
    char text[1024];

    if (!stream) {
        close(fds[0]);
    } else {
        while (fgets(text, sizeof text, stream)) {
            text[strcspn(text, "\n")] = 0;
            line(text);
        }

        fclose(stream);
    }

    int status;
    pid_t waited;

    do
        waited = waitpid(pid, &status, 0);
    while (waited < 0 && errno == EINTR);

    return stream && waited == pid && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#include "stdio.c"
