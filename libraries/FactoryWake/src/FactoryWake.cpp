#include "FactoryWake.h"
#include "FactoryWakeModel.h"
#include <esp_vfs.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
extern "C" {
#include "esp_wn_models.h"
char *get_model_base_path(void) { return const_cast<char *>("/factory-wake"); }
}
namespace {
struct File { const uint8_t *bytes; size_t length; const char *suffix; };
const File files[] = {{model_MODEL_INFO_, sizeof(model_MODEL_INFO_), "_MODEL_INFO_"},
    {modelwn9_index, sizeof(modelwn9_index), "wn9_index"}, {modelwn9_data, sizeof(modelwn9_data), "wn9_data"}};
struct OpenFile { const File *file = nullptr; size_t offset = 0; } openFiles[8];
bool mounted = false;
const esp_wn_iface_t *net = nullptr;
model_iface_data_t *model = nullptr;
const File *find(const char *path) {
    for (const auto &file : files) {
        const size_t size = strlen(path), suffix = strlen(file.suffix);
        if (size >= suffix && !strcmp(path + size - suffix, file.suffix)) return &file;
    }
    return nullptr;
}
int openFile(const char *path, int flags, int) {
    if ((flags & O_ACCMODE) != O_RDONLY) { errno = EROFS; return -1; }
    const File *file = find(path);
    if (!file) { errno = ENOENT; return -1; }
    for (int i = 0; i < 8; ++i) if (!openFiles[i].file) {
        openFiles[i].file = file; openFiles[i].offset = 0; return i;
    }
    errno = EMFILE; return -1;
}
ssize_t readFile(int fd, void *buffer, size_t size) {
    if (fd < 0 || fd >= 8 || !openFiles[fd].file) { errno = EBADF; return -1; }
    auto &file = openFiles[fd];
    const size_t remaining = file.offset < file.file->length ? file.file->length - file.offset : 0;
    if (size > remaining) size = remaining;
    if (size) memcpy(buffer, file.file->bytes + file.offset, size);
    file.offset += size; return size;
}
off_t seekFile(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= 8 || !openFiles[fd].file) { errno = EBADF; return -1; }
    auto &file = openFiles[fd];
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) { errno = EINVAL; return -1; }
    const int64_t next = (whence == SEEK_SET ? 0 : whence == SEEK_CUR ? file.offset : file.file->length) + int64_t(offset);
    if (next < 0 || next > INT32_MAX) { errno = EINVAL; return -1; }
    file.offset = next; return next;
}
int closeFile(int fd) {
    if (fd < 0 || fd >= 8 || !openFiles[fd].file) { errno = EBADF; return -1; }
    openFiles[fd].file = nullptr; return 0;
}
int fileStat(const File *file, struct stat *st) {
    if (!file) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof(*st)); st->st_mode = S_IFREG | 0444; st->st_size = file->length; return 0;
}
int fdStat(int fd, struct stat *st) {
    if (fd < 0 || fd >= 8) { errno = EBADF; return -1; }
    return fileStat(openFiles[fd].file, st);
}
int pathStat(const char *path, struct stat *st) { return fileStat(find(path), st); }
}
namespace FactoryWake {
bool begin() {
    if (model) return true;
    if (!mounted) {
        esp_vfs_t fs = {};
        fs.flags = ESP_VFS_FLAG_DEFAULT; fs.open = openFile; fs.read = readFile;
        fs.lseek = seekFile; fs.close = closeFile; fs.fstat = fdStat; fs.stat = pathStat;
        if (esp_vfs_register("/factory-wake", &fs, nullptr) != ESP_OK) return false;
        mounted = true;
    }
    net = esp_wn_handle_from_name("wn9_nihaoxiaozhi");
    if (!net) return false;
    model = net->create("wn9_nihaoxiaozhi", DET_MODE_90);
    if (!model) return false;
    if (net->get_samp_rate(model) != 16000 || net->get_channel_num(model) != 1) { end(); return false; }
    return true;
}
int samples() { return model ? net->get_samp_chunksize(model) : 0; }
bool detect(int16_t *pcm) { return model && net->detect(model, pcm) == WAKENET_DETECTED; }
void clear() { if (model) net->clean(model); }
void end() { if (model) net->destroy(model); model = nullptr; }
}
