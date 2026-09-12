// The file helpers every platform shares, over two primitives each platform
// provides: orb_os_stat and orb_os_fopen, the only two that see a native path.

uint64_t orb_os_file_mtime(const char* path) {
    orb_os_info info;

    return orb_os_stat(path, &info) ? info.mtime : 0;
}

bool orb_os_is_dir(const char* path) {
    orb_os_info info;

    return orb_os_stat(path, &info) && info.dir;
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
