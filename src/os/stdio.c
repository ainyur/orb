static int stdio_compare_entries(const void* a, const void* b) {
    return strcmp(a, b); // the name is the first field
}

u64 orb_os_file_mtime(const char* path) {
    orb_os_info info;

    return orb_os_stat(path, &info) ? info.mtime : 0;
}

int orb_os_list_dir(const char* dir, orb_os_entry* out, int max) {
    int n = orb_os_read_dir(dir, out, max);

    qsort(out, (usize)n, sizeof *out, stdio_compare_entries);
    return n;
}

bool orb_os_read_file(const char* path, orb_arena* into, u8_span* out) {
    orb_os_info info;

    if (!orb_os_stat(path, &info)) return false;
    if (info.size >= (u64)UINT32_MAX) return false;

    // push before opening: an exhausted arena may longjmp out of here
    u8* data = orb_arena_push(into, (usize)info.size + 1, 16);
    FILE* file = orb_os_fopen(path, "rb");

    if (!file) return false;

    out->len = (u32)fread(data, 1, (usize)info.size, file);
    data[out->len] = 0;
    out->elems = data;
    fclose(file);

    return out->len == info.size;
}

bool orb_os_write_file(const char* path, u8_span data) {
    FILE* file = orb_os_fopen(path, "wb");

    if (!file) return false;

    bool ok = fwrite(data.elems, 1, data.len, file) == data.len;

    return fclose(file) == 0 && ok;
}

bool orb_path_absolute(const char* path) {
#ifdef _WIN32
    return path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':');
#else
    return path[0] == '/';
#endif
}

void orb_path_join(orb_path out, const char* dir, const char* rel) {
    bool absolute = orb_path_absolute(rel);
    int n = absolute ? snprintf(out, ORB_PATH_MAX, "%s", rel)
                     : snprintf(out, ORB_PATH_MAX, "%s/%s", dir, rel);

    if (n >= ORB_PATH_MAX) orb_fatal("path too long: %s/%s", dir, rel);
}
