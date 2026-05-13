#define FUSE_USE_VERSION 28

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>

static char source_dir[4096];
#define VIRTUAL_FILE "tujuan.txt"
#define NUM_LOG_FILES 7

static void build_source_path(char *fpath, size_t size, const char *path)
{
    snprintf(fpath, size, "%s%s", source_dir, path);
}

static int is_virtual(const char *path)
{
    return (strcmp(path, "/" VIRTUAL_FILE) == 0);
}

/* Revisi: Menghasilkan format "Tujuan Mas Amba: <fragmen> \n" */
static char *generate_tujuan_content(void)
{
    size_t buf_size = 65536;
    char *content = calloc(1, buf_size);
    if (!content) return NULL;

    // Awali dengan header yang diminta soal
    strcpy(content, "Tujuan Mas Amba: ");

    for (int i = 1; i <= NUM_LOG_FILES; i++) {
        char filepath[4200]; // Ditingkatkan untuk menghindari warning
        snprintf(filepath, sizeof(filepath), "%s/%d.txt", source_dir, i);

        FILE *fp = fopen(filepath, "r");
        if (!fp) continue;

        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "KOORD:", 6) == 0) {
                char *ptr = line + 6; // Ambil konten setelah "KOORD:"
                // Hapus karakter newline/return di akhir fragmen
                ptr[strcspn(ptr, "\r\n")] = 0;
                
                // Tambahkan fragmen ke konten
                strcat(content, ptr);
            }
        }
        fclose(fp);
    }
    
    // Akhiri dengan tepat satu newline
    strcat(content, "\n");

    return content;
}

static int kenz_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode  = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    if (is_virtual(path)) {
        char *content = generate_tujuan_content();
        size_t len = content ? strlen(content) : 0;
        free(content);

        stbuf->st_mode  = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size  = (off_t)len;
        return 0;
    }

    char fpath[4096];
    build_source_path(fpath, sizeof(fpath), path);
    int res = lstat(fpath, stbuf);
    if (res == -1) return -errno;
    return 0;
}

static int kenz_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    (void) offset; (void) fi;
    char fpath[4096];

    if (strcmp(path, "/") == 0) snprintf(fpath, sizeof(fpath), "%s", source_dir);
    else build_source_path(fpath, sizeof(fpath), path);

    DIR *dp = opendir(fpath);
    if (dp == NULL) return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0)) break;
    }
    closedir(dp);

    if (strcmp(path, "/") == 0) {
        struct stat vst;
        memset(&vst, 0, sizeof(vst));
        vst.st_mode = S_IFREG | 0444;
        filler(buf, VIRTUAL_FILE, &vst, 0);
    }
    return 0;
}

static int kenz_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    (void) fi;
    if (is_virtual(path)) {
        char *content = generate_tujuan_content();
        if (!content) return -ENOMEM;
        size_t len = strlen(content);
        if (offset >= (off_t)len) { free(content); return 0; }
        if (offset + size > len) size = len - offset;
        memcpy(buf, content + offset, size);
        free(content);
        return (int)size;
    }

    char fpath[4096];
    build_source_path(fpath, sizeof(fpath), path);
    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;
    int res = pread(fd, buf, size, offset);
    close(fd);
    return (res == -1) ? -errno : res;
}

static struct fuse_operations kenz_oper = {
    .getattr = kenz_getattr,
    .readdir = kenz_readdir,
    .read    = kenz_read,
};

int main(int argc, char *argv[])
{
    if (argc < 3) return 1;
    if (realpath(argv[1], source_dir) == NULL) return 1;
    int fuse_argc = argc - 1;
    char **fuse_argv = malloc(sizeof(char *) * fuse_argc);
    fuse_argv[0] = argv[0];
    for (int i = 1; i < fuse_argc; i++) fuse_argv[i] = argv[i + 1];
    int ret = fuse_main(fuse_argc, fuse_argv, &kenz_oper, NULL);
    free(fuse_argv);
    return ret;
}
