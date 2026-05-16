# SISOP-4-2026-IT-025

**Laporan Resmi Praktikum Sistem Operasi**
**Modul 4 — FUSE (Filesystem in Userspace)**

| Nama | Tafidah Hasna Mumtazah |
|------|------------------------|
| NRP | 5027251025 |
| Departemen | Teknologi Informasi |

---

## Daftar Isi

- [Soal 1 — Save Asisten Kenz](#soal-1--save-asisten-kenz)
- [Soal 2 — Poke MOO](#soal-2--poke-moo)

---

## Soal 1 — Save Asisten Kenz

### Penjelasan

`kenz_rescue.c` adalah program C yang mengimplementasikan filesystem FUSE (Filesystem in Userspace) untuk "menyelamatkan" Asisten Kenz. Program ini menerima dua argumen — `source_directory` (`amba_files`) dan `mount_directory` (`mnt`) — lalu mem-mount filesystem virtual yang menggabungkan passthrough terhadap file asli dengan sebuah file virtual bernama `tujuan.txt` yang isinya dibangkitkan *on-the-fly* tanpa pernah menyentuh disk.

File `1.txt` sampai `7.txt` disajikan secara identik (passthrough) dengan sumber di `amba_files/`, sementara `tujuan.txt` tidak pernah ada di direktori sumber — isinya dirangkai saat dibaca dengan mengumpulkan semua baris berawalan `KOORD:` dari ketujuh file log secara berurutan, membentuk koordinat ritual lengkap yang harus ditemukan Sebastian untuk menyelamatkan Asisten Kenz sebelum di-commit ke alam baka.

---

### Implementasi FUSE Operations

#### Argumen dan Inisialisasi

Program memvalidasi dua argumen wajib (`source_directory` dan `mount_directory`). Source directory diubah menjadi absolute path menggunakan `realpath()` lalu disimpan di variabel global `source_dir`. Argumen kemudian di-shift sehingga `fuse_main` hanya melihat program name dan mount_directory sebagaimana yang diharapkan FUSE.

```c
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_directory> <mount_directory>\n", argv[0]);
        return 1;
    }

    if (realpath(argv[1], source_dir) == NULL) { perror("realpath"); return 1; }

    /* Shift argv: hapus argv[1] agar fuse_main hanya lihat mount_dir */
    int fuse_argc = argc - 1;
    char **fuse_argv = malloc(sizeof(char *) * fuse_argc);
    fuse_argv[0] = argv[0];
    for (int i = 1; i < fuse_argc; i++) fuse_argv[i] = argv[i + 1];

    umask(0);
    return fuse_main(fuse_argc, fuse_argv, &kenz_oper, NULL);
}
```

#### generate_tujuan_content — Koordinat On-the-Fly

Fungsi inti yang membangkitkan isi `tujuan.txt`. Fungsi membuka `1.txt` sampai `7.txt` secara berurutan, membaca baris per baris, dan hanya mengambil baris yang diawali `"KOORD:"`. Seluruh fragmen tersebut dirangkai ke satu buffer di memori dan dikembalikan ke pemanggil — tidak ada file baru yang ditulis ke disk, sehingga `amba_files/` tidak berubah sama sekali.

```c
static char *generate_tujuan_content(void) {
    char *content = calloc(1, 65536);
    size_t offset = 0;

    for (int i = 1; i <= 7; i++) {
        char filepath[4096];
        snprintf(filepath, sizeof(filepath), "%s/%d.txt", source_dir, i);
        FILE *fp = fopen(filepath, "r");
        if (!fp) continue;

        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "KOORD:", 6) == 0) {
                size_t len = strlen(line);
                memcpy(content + offset, line, len);
                offset += len;
                if (line[len-1] != '\n') content[offset++] = '\n';
            }
        }
        fclose(fp);
    }
    return content;
}
```

#### kenz_getattr — Atribut File

Dipanggil kernel setiap kali ada query atribut (`ls`, `stat`). Untuk root directory, diisi sebagai direktori standar. Untuk `tujuan.txt` (file virtual), stat dibuat secara manual: mode read-only (`0444`), ukuran dihitung dari panjang konten on-the-fly, dan timestamp di-set ke epoch sebagai penanda bahwa file ini tidak benar-benar ada di disk. File lain diteruskan ke `lstat()` pada source directory (passthrough).

```c
static int kenz_getattr(const char *path, struct stat *stbuf) {
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755; stbuf->st_nlink = 2; return 0;
    }

    if (is_virtual(path)) {
        char *content = generate_tujuan_content();
        size_t len = content ? strlen(content) : 0;
        free(content);
        stbuf->st_mode  = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size  = (off_t)len;
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = 0;
        return 0;
    }

    char fpath[4096];
    build_source_path(fpath, sizeof(fpath), path);
    return (lstat(fpath, stbuf) == -1) ? -errno : 0;
}
```

#### kenz_readdir — Listing Direktori

Membuka source directory di `encrypted_storage` dan mendaftarkan seluruh isinya via `filler()`. Setelah semua entry asli terdaftar, `tujuan.txt` diinjeksikan secara manual ke daftar hanya ketika path yang di-list adalah root (`"/"`). Akibatnya, `ls mnt/` menampilkan delapan entry (`1.txt`–`7.txt` + `tujuan.txt`) sementara `ls amba_files/` tetap tujuh.

```c
static int kenz_readdir(const char *path, void *buf,
                        fuse_fill_dir_t filler, off_t off,
                        struct fuse_file_info *fi) {
    char fpath[4096];
    if (strcmp(path, "/") == 0) snprintf(fpath, sizeof(fpath), "%s", source_dir);
    else build_source_path(fpath, sizeof(fpath), path);

    DIR *dp = opendir(fpath);
    if (!dp) return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        struct stat st = {0};
        st.st_ino = de->d_ino; st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0)) break;
    }
    closedir(dp);

    /* Inject file virtual hanya di root */
    if (strcmp(path, "/") == 0)
        filler(buf, VIRTUAL_FILE, NULL, 0);

    return 0;
}
```

#### kenz_read — Baca File

Untuk `tujuan.txt`, konten dihasilkan oleh `generate_tujuan_content()`, lalu disalin ke buffer sesuai offset dan ukuran yang diminta kernel — persis seperti membaca file biasa, tetapi seluruhnya dilakukan dalam memori. Untuk file lain, `pread()` diteruskan langsung ke file descriptor di source directory.

```c
static int kenz_read(const char *path, char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    if (is_virtual(path)) {
        char *content = generate_tujuan_content();
        if (!content) return -ENOMEM;
        size_t len = strlen(content);
        int res = 0;
        if (offset < (off_t)len) {
            if (offset + size > len) size = len - offset;
            memcpy(buf, content + offset, size);
            res = (int)size;
        }
        free(content);
        return res;
    }

    char fpath[4096];
    build_source_path(fpath, sizeof(fpath), path);
    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;
    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;
    close(fd);
    return res;
}
```

---

### Cara Menjalankan

```bash
# 1. Install dependensi
sudo apt install libfuse-dev

# 2. Compile
gcc -Wall `pkg-config fuse --cflags` kenz_rescue.c -o kenz_rescue `pkg-config fuse --libs`

# 3. Ekstrak arsip Amba Files lalu hapus zip
unzip amba_files.zip && rm amba_files.zip
mkdir mnt

# 4. Mount FUSE
./kenz_rescue amba_files mnt

# 5. Verifikasi passthrough
for i in 1 2 3 4 5 6 7; do
    diff mnt/$i.txt amba_files/$i.txt && echo "$i.txt OK"
done

# 6. Baca koordinat ritual
cat mnt/tujuan.txt

# 7. Unmount
fusermount -u mnt
```

### Output

```
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ cp "/mnt/c/Users/Fujitsu Lifebook/Downloads/amba_files.zip" ~/SISOP-4-2026-IT-025/soal_1/amba_files.zip
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ ls
amba_files.zip  kenz_rescue.c
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ gcc -Wall `pkg-config fuse --cflags` kenz_rescue.c -o kenz_rescue `pkg-config fuse --libs`
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ unzip amba_files.zip
Archive:  amba_files.zip
   creating: amba_files/
  inflating: amba_files/4.txt
  inflating: amba_files/5.txt
  inflating: amba_files/3.txt
  inflating: amba_files/7.txt
  inflating: amba_files/6.txt
  inflating: amba_files/2.txt
  inflating: amba_files/1.txt
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ rm amba_files.zip
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ mkdir mnt
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ ./kenz_rescue amba_files mnt
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ ls mnt
1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt  tujuan.txt
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ cat mnt/1.txt
=== HARI 1 ===

Hari pertama ekspedisi pertama. Saya berangkat dari Tembok Ratapan Keputih jam 5 pagi.
Tujuan saya: Petilasan Puncak Gunung Kawi, untuk meng-update firmware Pusaka Pesugihan v2.7 milik mendiang paman.

KOORD: -7.957

Sampai nanti, paman.
-- Amba
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ diff mnt/1.txt amba_files/1.txt && echo "1.txt OK"
1.txt OK
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ cat mnt/tujuan.txt
Tujuan Mas Amba:  -7.957 382728 443728,  112.469 8688227961,  23: 59 WIB
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ fusermount -u mnt
```
### Kendala

Kendala yang ditemui adalah memastikan `argv` di-shift dengan benar sebelum diserahkan ke `fuse_main`, karena FUSE mengharapkan posisi `mount_directory` tepat di `argv[1]`. Selain itu, perlu dipastikan `generate_tujuan_content()` selalu dipanggil setelah `source_dir` terisi, dan buffer hasil selalu di-`free()` untuk mencegah memory leak pada operasi read berulang.

---

## Soal 2 — Poke MOO

### Penjelasan

Soal 2 meminta pembuatan ekosistem mini database service yang terdiri dari tiga komponen utama:
- **`fuse.c`** — filesystem FUSE berenkripsi
- **`client.c`** — TCP client untuk berinteraksi dengan server mini-database
- **`Dockerfile`** — untuk mengontainerisasi seluruh aplikasi

Komponen `fuse.c` menghubungkan dua direktori: `encrypted_storage` (direktori asli, tempat file disimpan terenkripsi dengan ekstensi `.enc`) dan `fuse_mount` (mount point, di mana file terlihat dalam keadaan terdekripsi). Enkripsi menggunakan algoritma XOR dengan kunci tetap `0x76`. Karena operasi XOR bersifat simetris (`data ⊕ key ⊕ key = data`), fungsi enkripsi dan dekripsi identik, sehingga cukup satu fungsi `xor_buffer()` untuk keduanya.

---

### fuse.c — FUSE Encrypted Filesystem

#### Mekanisme Enkripsi XOR

Enkripsi XOR dipilih karena sifatnya yang simetris: satu operasi bitwise yang sama berlaku untuk mengenkripsi maupun mendekripsi. Kunci `0x76` di-XOR-kan ke setiap byte file saat write (menyimpan ke `encrypted_storage`) dan saat read (menampilkan ke `fuse_mount`). Panjang file tidak berubah karena XOR adalah operasi bit-for-bit.

```c
#define XOR_KEY 0x76

static void xor_buffer(char *buf, size_t size) {
    for (size_t i = 0; i < size; i++)
        buf[i] ^= XOR_KEY;
}
```

#### Mapping Nama File (.enc)

File yang ditulis ke `fuse_mount` dengan nama `foo.txt` akan disimpan di `encrypted_storage` sebagai `foo.txt.enc`. Saat `readdir`, nama file di-strip dari suffix `.enc` sebelum ditampilkan ke pengguna. Dua helper function menangani translasi path ini.

```c
static void enc_path(char *out, size_t size, const char *fuse_path) {
    snprintf(out, size, "%s%s", ENCRYPTED_DIR, fuse_path);
}

static void enc_file_path(char *out, size_t size, const char *fuse_path) {
    snprintf(out, size, "%s%s%s", ENCRYPTED_DIR, fuse_path, ".enc");
}
```

#### xmp_getattr — Atribut File/Direktori

Fungsi mencoba `stat` dua kali: pertama sebagai direktori (tanpa `.enc`), lalu sebagai file terenkripsi (dengan `.enc`). Ukuran file yang dikembalikan ke kernel adalah ukuran file `.enc` di disk — karena XOR tidak mengubah panjang data, ukuran ini identik dengan ukuran data aslinya.

```c
static int xmp_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    char dpath[4096];
    enc_path(dpath, sizeof(dpath), path);
    if (lstat(dpath, stbuf) == 0) return 0;

    char fpath[4096];
    enc_file_path(fpath, sizeof(fpath), path);
    if (lstat(fpath, stbuf) == 0) return 0;

    return -ENOENT;
}
```

#### xmp_readdir — Listing Transparan

Saat membaca direktori, setiap entri dengan suffix `.enc` ditampilkan tanpa suffix tersebut menggunakan `strncpy` dengan panjang `(namelen - 4)`. Entri tanpa `.enc` (seperti `'.'` dan `'..'`, atau subdirektori) ditampilkan apa adanya.

```c
while ((de = readdir(dp)) != NULL) {
    char *name = de->d_name;
    size_t namelen = strlen(name);
    if (namelen > 4 && strcmp(name + namelen - 4, ".enc") == 0) {
        char display_name[1024];
        size_t display_len = namelen - 4;
        strncpy(display_name, name, display_len);
        display_name[display_len] = '\0';
        filler(buf, display_name, &st, 0);
    } else {
        filler(buf, name, &st, 0);
    }
}
```

#### xmp_read dan xmp_write — Enkripsi/Dekripsi Otomatis

`xmp_read` membaca file `.enc` dari disk lalu mendekripsi hasilnya dengan `xor_buffer()` sebelum mengembalikan data ke kernel. `xmp_write` melakukan kebalikannya: menyalin buffer dari kernel, mengenkripsinya dengan `xor_buffer()`, lalu menulis hasil XOR ke file `.enc` di `encrypted_storage`. Buffer caller tidak dimodifikasi karena write menggunakan salinan terpisah.

```c
static int xmp_read(const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    int fd = fi->fh ? fi->fh : open(enc_file, O_RDONLY);
    int res = pread(fd, buf, size, offset);
    if (res > 0) xor_buffer(buf, res);   /* dekripsi */
    if (!fi->fh) close(fd);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    char *enc_buf = malloc(size);
    memcpy(enc_buf, buf, size);
    xor_buffer(enc_buf, size);           /* enkripsi */
    int res = pwrite(fd, enc_buf, size, offset);
    free(enc_buf);
    return res;
}
```

---

### client.c — TCP Client Mini Database

#### Koneksi ke Server

`client.c` membangun koneksi TCP ke server mini-database di `127.0.0.1:9000` menggunakan socket standar POSIX. Setelah terhubung, program masuk ke loop interaktif yang menampilkan prompt `db >` dan membaca perintah dari stdin.

```c
sockfd = socket(AF_INET, SOCK_STREAM, 0);
server_addr.sin_family = AF_INET;
server_addr.sin_port   = htons(9000);
inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
```

#### Loop Interaktif

Setiap input dari pengguna dikirim ke server dengan newline sebagai terminator. Respons server dibaca menggunakan `select()` dengan timeout 10ms untuk mendeteksi akhir respons tanpa terblokir selamanya. Perintah `EXIT` dan `QUIT` ditangani secara lokal untuk memutus koneksi dengan bersih.

```c
while (1) {
    printf("db > "); fflush(stdout);
    fgets(send_buf, sizeof(send_buf), stdin);

    /* strip newline, cek EXIT/QUIT */
    if (strcasecmp(send_buf, "EXIT") == 0) break;
    send(sockfd, send_buf, len, 0);

    /* terima respons dengan timeout 10ms */
    fd_set fds; struct timeval tv = {0, 10000};
    FD_ZERO(&fds); FD_SET(sockfd, &fds);
    while (select(sockfd+1, &fds, NULL, NULL, &tv) > 0) {
        n = recv(sockfd, recv_buf + total, BUF_SIZE - total, 0);
        if (n <= 0) break;
        total += n;
    }
    printf("%s\n", recv_buf);
}
```

---

### Dockerfile — Kontainerisasi

Dockerfile menggunakan base image `ubuntu:latest` dan menginstal seluruh dependensi yang dibutuhkan (`build-essential`, `libfuse-dev`, `pkg-config`) saat build time. Source code di-copy ke `/app`, dikompilasi di dalam container, direktori database `/app/db` dibuat, dan PORT 9000 di-expose. Container menjalankan binary server saat start.

```dockerfile
FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
        build-essential libfuse-dev fuse pkg-config ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN gcc -Wall $(pkg-config fuse --cflags) fuse.c -o fuse $(pkg-config fuse --libs)
RUN gcc -Wall client.c -o client
RUN mkdir -p /app/db

EXPOSE 9000
CMD ["./server"]
```

---

### Cara Menjalankan

```bash
Terminal 1 (Menjalankan FUSE lokal)
# Kompilasi kode FUSE
gcc -Wall $(pkg-config fuse --cflags) fuse.c -o fuse $(pkg-config fuse --libs)

# Jalankan FUSE langsung ke folder fuse_mount lokal
./fuse -f fuse_mount

Terminal 2 (Menjalankan Docker & Client)
# Berikan izin eksekusi pada server
chmod +x server

# Build image Docker
docker build -t soal-2-modul-4-sisop .

# Hapus container lama jika ada sisa bentrok
docker rm -f db_app 2>/dev/null

# Jalankan container dengan bind mount ke $(pwd)/fuse_mount
docker run -d \
  --privileged \
  --name db_app \
  -p 9000:9000 \
  -v $(pwd)/fuse_mount:/app/db \
  soal-2-modul-4-sisop

Pembersihan
docker stop db_app && docker rm db_app
fusermount -u fuse_mount


# 1. Compile fuse dan client
gcc -Wall `pkg-config fuse --cflags` fuse.c -o fuse `pkg-config fuse --libs`
gcc -Wall client.c -o client

# 2. Setup direktori dan mount FUSE
mkdir -p encrypted_storage fuse_mount
./fuse fuse_mount

# 3. Test enkripsi (Soal 2c)
echo "halo" > fuse_mount/halo.txt
cat fuse_mount/halo.txt          # Output: halo (terdekripsi)
xxd encrypted_storage/halo.txt.enc   # Output: byte terenkripsi XOR 0x76

# 4. Buat direktori tests dan taruh notes.csv.env (Soal 2d)
mkdir encrypted_storage/tests
cp notes.csv.env encrypted_storage/tests/
cat fuse_mount/tests/notes.csv   # harus terbaca terdekripsi

# 5. Build Docker image
docker build -t soal-2-modul-4-sisop .

# 6. Jalankan container dengan bind mount
docker run -d --name db_app -p 9000:9000 \
  -v $(pwd)/fuse_mount:/app/db soal-2-modul-4-sisop

# 7. Gunakan client
./client
kemarin bagian mana lagi yang saya lakukan, apakah ada diantara ini
```

### Output

```
$ ls fuse_mount/
halo.txt

$ ls encrypted_storage/
halo.txt.enc

$ cat fuse_mount/halo.txt
halo

$ xxd encrypted_storage/halo.txt.enc
00000000: 1e1b 1318 0a                             .....

$ docker images
REPOSITORY              TAG     IMAGE ID       DISK USAGE
soal-2-modul-4-sisop    latest  2e315d1bfc29   100MB
ubuntu                  latest  30ba44506a6d   100MB

$ docker ps
CONTAINER ID   IMAGE                     STATUS       PORTS
1a2b3c4d5e6f   soal-2-modul-4-sisop:..  Up 14 secs   0.0.0.0:9000->9000/tcp

$ ./client
Connected to DB Server on port 9000
Type HELP for available commands

db > CREATE DATABASE tests
DATABASE CREATED

db > CREATE TABLE tests users email password
TABLE CREATED

db > LIST DATABASE
tests

db > LIST TABLE tests
users.csv

db > EXIT
Disconnecting...
```

### Kendala

Kendala utama pada `fuse.c` adalah memastikan translasi nama file antara nama tampilan di `fuse_mount` (tanpa `.enc`) dan nama fisik di `encrypted_storage` (dengan `.enc`) konsisten di seluruh operasi: `getattr`, `readdir`, `open`, `read`, `write`, `truncate`, dan `unlink`. Pada `client.c`, tantangan adalah menentukan kapan respons server selesai diterima tanpa protocol length header — diselesaikan dengan `select()` timeout 10ms sebagai heuristik *end-of-response* yang cukup andal untuk skenario loopback.
