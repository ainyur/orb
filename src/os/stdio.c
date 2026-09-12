// The file helpers every platform shares, over the primitives each platform
// provides: orb_os_stat, orb_os_fopen, and orb_os_read_dir, the only ones that
// see a native path.

static int stdio_compare_entries(const void* a, const void* b) {
    return strcmp(a, b); // the name is the first field
}

uint64_t orb_os_file_mtime(const char* path) {
    orb_os_info info;

    return orb_os_stat(path, &info) ? info.mtime : 0;
}

int orb_os_list_dir(const char* dir, orb_os_entry* out, int max) {
    int n = orb_os_read_dir(dir, out, max);

    qsort(out, (size_t)n, sizeof *out, stdio_compare_entries);
    return n;
}

bool orb_os_read_file(const char* path, orb_arena* into, orb_span* out) {
    orb_os_info info;

    if (!orb_os_stat(path, &info)) return false;

    // push before opening: an exhausted arena may longjmp out of here
    uint8_t* data = orb_arena_push(into, (size_t)info.size + 1, 16);
    FILE* f = orb_os_fopen(path, "rb");

    if (!f) return false;

    out->len = fread(data, 1, (size_t)info.size, f);
    data[out->len] = 0;
    out->ptr = data;
    fclose(f);

    return out->len == info.size;
}

bool orb_os_write_file(const char* path, orb_span data) {
    FILE* f = orb_os_fopen(path, "wb");

    if (!f) return false;

    bool ok = fwrite(data.ptr, 1, data.len, f) == data.len;

    return fclose(f) == 0 && ok;
}

void orb_path_join(orb_path out, const char* dir, const char* rel) {
#ifdef _WIN32
    bool absolute = rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':');
#else
    bool absolute = rel[0] == '/';
#endif
    int n = absolute ? snprintf(out, ORB_PATH_MAX, "%s", rel)
                     : snprintf(out, ORB_PATH_MAX, "%s/%s", dir, rel);

    if (n >= ORB_PATH_MAX) orb_fatal("path too long: %s/%s", dir, rel);
}
