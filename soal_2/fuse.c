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
#include <sys/types.h>
#include <utime.h>

#define XOR_KEY 0x76

static const char *ENCRYPTED_DIR = "./encrypted_storage";

#define ENC_SUFFIX ".enc"
#define ENC_SUFFIX_LEN 4

static void enc_path(char *out, size_t size, const char *fuse_path)
{
    snprintf(out, size, "%s%s", ENCRYPTED_DIR, fuse_path);
}

static void enc_file_path(char *out, size_t size, const char *fuse_path)
{
    snprintf(out, size, "%s%s%s", ENCRYPTED_DIR, fuse_path, ENC_SUFFIX);
}

static void xor_buffer(char *buf, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        buf[i] ^= XOR_KEY;
    }
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    char dpath[4096];
    enc_path(dpath, sizeof(dpath), path);

    if (lstat(dpath, stbuf) == 0) {
        return 0;
    }

    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);

    if (lstat(fpath, stbuf) == 0) {
        return 0;
    }

    return -ENOENT;
}

/* access */
static int xmp_access(const char *path, int mask)
{
    char dpath[4096];
    enc_path(dpath, sizeof(dpath), path);
    if (access(dpath, mask) == 0) return 0;

    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);
    if (access(fpath, mask) == 0) return 0;

    return -EACCES;
}

/* readdir */
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    char dpath[4096];
    enc_path(dpath, sizeof(dpath), path);

    DIR *dp = opendir(dpath);
    if (dp == NULL) return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino  = de->d_ino;
        st.st_mode = de->d_type << 12;

        char *name = de->d_name;
        size_t namelen = strlen(name);

        if (namelen > ENC_SUFFIX_LEN &&
            strcmp(name + namelen - ENC_SUFFIX_LEN, ENC_SUFFIX) == 0) {
            char display_name[1024];
            size_t display_len = namelen - ENC_SUFFIX_LEN;
            strncpy(display_name, name, display_len);
            display_name[display_len] = '\0';
            if (filler(buf, display_name, &st, 0)) break;
        } else {
            if (filler(buf, name, &st, 0)) break;
        }
    }
    closedir(dp);
    return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
    char dpath[4096];
    enc_path(dpath, sizeof(dpath), path);
    if (mkdir(dpath, mode) == -1) return -errno;
    return 0;
}

static int xmp_rmdir(const char *path)
{
    char dpath[4096];
    enc_path(dpath, sizeof(dpath), path);
    if (rmdir(dpath) == -1) return -errno;
    return 0;
}

static int xmp_unlink(const char *path)
{
    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);
    if (unlink(fpath) == -1) return -errno;
    return 0;
}

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);

    int fd = open(fpath, fi->flags | O_CREAT | O_TRUNC, mode);
    if (fd == -1) return -errno;

    fi->fh = fd;
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);

    int fd = open(fpath, fi->flags);
    if (fd == -1) return -errno;

    fi->fh = fd;
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    int fd;
    int res;

    if (fi->fh) {
        fd = fi->fh;
    } else {
        char fpath[4096];
        enc_file_path(fpath, sizeof(fpath), path);
        fd = open(fpath, O_RDONLY);
        if (fd == -1) return -errno;
    }

    res = pread(fd, buf, size, offset);
    if (res == -1) {
        res = -errno;
    } else {
        xor_buffer(buf, res);
    }

    if (!fi->fh) close(fd);
    return res;
}
static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    char *enc_buf = malloc(size);
    if (!enc_buf) return -ENOMEM;
    memcpy(enc_buf, buf, size);
    xor_buffer(enc_buf, size);

    int fd;
    int res;

    if (fi->fh) {
        fd = fi->fh;
    } else {
        char fpath[4096];
        enc_file_path(fpath, sizeof(fpath), path);
        fd = open(fpath, O_WRONLY);
        if (fd == -1) {
            free(enc_buf);
            return -errno;
        }
    }

    res = pwrite(fd, enc_buf, size, offset);
    if (res == -1) res = -errno;

    if (!fi->fh) close(fd);
    free(enc_buf);
    return res;
}

/* truncate */
static int xmp_truncate(const char *path, off_t size)
{
    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);
    if (truncate(fpath, size) == -1) return -errno;
    return 0;
}

/* utimens */
static int xmp_utimens(const char *path, const struct timespec ts[2])
{
    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);

    struct timeval tv[2];
    tv[0].tv_sec  = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec  = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    if (utimes(fpath, tv) == -1) return -errno;
    return 0;
}

/* release */
static int xmp_release(const char *path, struct fuse_file_info *fi)
{
    (void) path;
    if (fi->fh) close(fi->fh);
    return 0;
}

static struct fuse_operations xmp_oper = {
    .getattr  = xmp_getattr,
    .access   = xmp_access,
    .readdir  = xmp_readdir,
    .mkdir    = xmp_mkdir,
    .rmdir    = xmp_rmdir,
    .unlink   = xmp_unlink,
    .create   = xmp_create,
    .open     = xmp_open,
    .read     = xmp_read,
    .write    = xmp_write,
    .truncate = xmp_truncate,
    .utimens  = xmp_utimens,
    .release  = xmp_release,
};

int main(int argc, char *argv[])
{
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}
