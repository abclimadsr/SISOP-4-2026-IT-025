# SISOP-4-2026-IT-025

## Laporan Resmi Praktikum Sistem Operasi
### Modul 4 — File System & FUSE

| Atribut | Keterangan |
|---|---|
| **Nama** | Tafidah Hasna Mumtazah |
| **NRP** | 5027251025 |
| **Departemen** | Teknologi Informasi |

---

## Daftar Isi
- [Soal 1 — Save Asisten Kenz](#soal-1--save-asisten-kenz)
- [Soal 2 — Poke MOO](#soal-2--poke-moo)

---

## Soal 1 — Save Asisten Kenz

### Penjelasan

`kenz_rescue.c` adalah program C yang mengimplementasikan filesystem FUSE (Filesystem in Userspace) untuk "menyelamatkan" Asisten Kenz. FUSE adalah sebuah interface di mana kita dapat membuat file system sendiri pada userspace Linux. Keuntungannya, kita bisa menggunakan library apapun yang tersedia tanpa perlu mengenali secara mendalam apa yang file system lakukan di kernel space — modul FUSE menjembatani antara kode file system di userspace dengan file system di kernel space. Saat user berurusan dengan operasi seperti `read`, `write`, atau `stat` di mount directory, kernel meneruskan request tersebut ke program FUSE melalui `/dev/fuse`, lalu program merespons kembali ke user.

Program menerima dua argumen: `source_directory` (`amba_files`) dan `mount_directory` (`mnt`). Secara garis besar, program ini melakukan dua hal: menyajikan file `1.txt` sampai `7.txt` secara identik (passthrough) dengan sumber di `amba_files/`, dan menambahkan satu file virtual bernama `tujuan.txt` yang isinya dibangkitkan secara *on-the-fly* tanpa pernah ditulis ke disk. Isi `tujuan.txt` dirangkai dengan mengumpulkan semua baris berawalan `KOORD:` dari ketujuh file log secara berurutan untuk membentuk koordinat ritual lengkap yang harus ditemukan Sebastian untuk menyelamatkan Asisten Kenz.

Program ini mengimplementasikan tiga fungsi wajib FUSE yang terdaftar dalam `struct fuse_operations`, yaitu `getattr` (dipanggil saat sistem mencoba mendapatkan atribut file), `readdir` (dipanggil saat user menampilkan isi direktori), dan `read` (dipanggil saat sistem membaca data dari file), ditambah `open` sebagai fungsi pendukung.

### Fungsi `generate_tujuan_content` — Pembangkit Koordinat

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

Fungsi ini adalah inti dari file virtual `tujuan.txt`. Ia membuka `1.txt` sampai `7.txt` dari `source_dir` secara berurutan, membaca baris per baris, dan hanya mengambil baris yang diawali `"KOORD:"`. Seluruh fragmen koordinat tersebut dirangkai ke satu buffer di memori menggunakan `memcpy` lalu dikembalikan ke pemanggil. Tidak ada file baru yang ditulis ke disk sehingga `amba_files/` tidak berubah sama sekali. Pemanggil wajib membebaskan buffer ini dengan `free()` setelah selesai.

### Fungsi `kenz_getattr` — Atribut File

```c
static int kenz_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
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
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = 0;
        return 0;
    }

    char fpath[4096];
    build_source_path(fpath, sizeof(fpath), path);
    return (lstat(fpath, stbuf) == -1) ? -errno : 0;
}
```

`getattr` adalah fungsi FUSE wajib yang dipanggil kernel setiap kali ada query atribut seperti `ls` atau `stat`. Fungsi ini menangani tiga kasus: root directory diisi sebagai direktori standar dengan mode `0755`; `tujuan.txt` mendapat stat buatan manual dengan mode read-only `0444`, ukuran dihitung dari panjang konten *on-the-fly*, dan timestamp di-set ke epoch sebagai penanda file ini tidak ada di disk; dan file lainnya diteruskan ke `lstat()` pada source directory sebagai passthrough murni.

### Fungsi `kenz_readdir` — Listing Direktori

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
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0)) break;
    }
    closedir(dp);

    if (strcmp(path, "/") == 0)
        filler(buf, VIRTUAL_FILE, NULL, 0);

    return 0;
}
```

`readdir` adalah fungsi FUSE wajib yang dipanggil saat user mencoba menampilkan file dan direktori pada suatu direktori. Ia membuka source directory, mendaftarkan seluruh isinya ke kernel via `filler()`, lalu setelah semua entry asli selesai, menginjeksikan `tujuan.txt` secara manual hanya ketika path yang di-list adalah root (`"/"`). Akibatnya, `ls mnt/` menampilkan delapan entry (`1.txt`–`7.txt` + `tujuan.txt`) sementara `ls amba_files/` tetap tujuh, sesuai permintaan soal.

### Fungsi `kenz_read` — Membaca File

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

`read` adalah fungsi FUSE wajib yang dipanggil saat sistem mencoba membaca potongan demi potongan data dari suatu file. Untuk `tujuan.txt`, konten dihasilkan oleh `generate_tujuan_content()` lalu disalin ke buffer kernel sesuai `offset` dan `size` yang diminta — seluruhnya terjadi di memori tanpa menyentuh disk. Untuk file lain, `pread()` diteruskan langsung ke file descriptor di source directory. Penggunaan `pread()` penting agar pembacaan bisa dimulai dari posisi tertentu tanpa memindahkan file pointer global.

### Fungsi `main` — Inisialisasi FUSE

```c
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_directory> <mount_directory>\n", argv[0]);
        return 1;
    }

    if (realpath(argv[1], source_dir) == NULL) {
        perror("realpath source_directory");
        return 1;
    }

    int fuse_argc = argc - 1;
    char **fuse_argv = malloc(sizeof(char *) * fuse_argc);
    fuse_argv[0] = argv[0];
    for (int i = 1; i < fuse_argc; i++) fuse_argv[i] = argv[i + 1];

    umask(0);
    int ret = fuse_main(fuse_argc, fuse_argv, &kenz_oper, NULL);
    free(fuse_argv);
    return ret;
}
```

`fuse_main()` adalah fungsi utama FUSE di userspace: ia memanggil `fuse_mount()` untuk membuat UNIX domain socket, lalu `fuse_loop()` yang membaca file system calls dari `/dev/fuse` secara terus-menerus. Program memvalidasi dua argumen wajib, mengubah `source_directory` menjadi absolute path menggunakan `realpath()`, lalu men-shift argv agar `fuse_main` hanya melihat program name dan mount_directory. Tanpa shift ini, `fuse_main` akan salah menginterpretasikan `source_directory` sebagai opsi FUSE.

### Output

**Kompilasi dan persiapan**

```bash
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
```

**Listing mount point vs source directory**

```bash
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ ls mnt
1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt  tujuan.txt
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_1$ ls amba_files
1.txt  2.txt  3.txt  4.txt  5.txt  6.txt  7.txt
```

`tujuan.txt` muncul di `mnt/` tetapi tidak ada di `amba_files/`, membuktikan file virtual berhasil diinjeksikan oleh `kenz_readdir` tanpa mengubah source directory.

**Verifikasi passthrough dan koordinat ritual**

```bash
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

Isi `mnt/1.txt` identik byte-per-byte dengan `amba_files/1.txt` (diff tidak menghasilkan perbedaan). Isi `tujuan.txt` dirangkai secara *on-the-fly* dari baris `KOORD:` di ketujuh file tanpa pernah ditulis ke disk. Untuk unmount FUSE digunakan `fusermount -u [direktori tujuan]` yang menginformasikan ke sistem untuk menyelesaikan semua operasi yang masih tertunda sebelum melepas filesystem.

### Kendala

Tidak ditemukan kendala selama pengerjaan.

---

## Soal 2 — Poke MOO

### Penjelasan

Soal 2 meminta pembuatan ekosistem mini database service yang memanfaatkan FUSE lebih dari sekadar passthrough. `fuse.c` mengimplementasikan filesystem FUSE yang berfungsi sebagai lapisan enkripsi transparan, menghubungkan dua direktori: `encrypted_storage` sebagai direktori asli tempat file disimpan terenkripsi dengan ekstensi `.enc`, dan `fuse_mount` sebagai mount point tempat file terlihat terdekripsi dengan nama aslinya. Enkripsi menggunakan algoritma XOR dengan kunci tetap `0x76` — karena XOR bersifat simetris (`data ⊕ key ⊕ key = data`), fungsi enkripsi dan dekripsi identik, cukup satu fungsi `xor_buffer()`. Ini adalah contoh konkret dari pernyataan di materi bahwa FUSE dapat diimplementasikan "lebih dari sekadar pure passthrough" dengan menambahkan fitur seperti enkripsi file secara transparan.

Seluruh ekosistem kemudian dikontainerisasi menggunakan Docker. Docker adalah platform yang memungkinkan pengembang mengemas aplikasi beserta dependensinya ke dalam container yang dapat dijalankan secara konsisten di berbagai lingkungan. Berbeda dengan virtualisasi yang memerlukan sistem operasi penuh untuk setiap mesin virtual, container berbagi kernel OS host sehingga lebih ringan dan proses start-up lebih cepat. Integrasi FUSE dengan Docker dilakukan melalui mekanisme bind mount: direktori `fuse_mount` di host di-mount ke dalam container menggunakan `-v $(pwd)/fuse_mount:/app/db`, sehingga server di dalam container dapat mengakses file melalui lapisan enkripsi FUSE secara transparan. Selain `fuse.c` dan `Dockerfile`, juga dibuat `client.c` sebagai TCP client untuk berinteraksi dengan server mini-database di port 9000.

### Fungsi `xor_buffer` — Enkripsi XOR

```c
#define XOR_KEY 0x76

static void xor_buffer(char *buf, size_t size) {
    for (size_t i = 0; i < size; i++)
        buf[i] ^= XOR_KEY;
}
```

Satu fungsi ini menangani baik enkripsi maupun dekripsi karena XOR adalah operasi invers terhadap dirinya sendiri: `data ⊕ key ⊕ key = data`. Kunci `0x76` di-XOR-kan ke setiap byte data secara berurutan. Panjang data tidak berubah karena XOR adalah operasi bit-for-bit tanpa padding, sehingga ukuran file `.enc` di `encrypted_storage` selalu identik dengan ukuran file aslinya.

### Fungsi `enc_path` dan `enc_file_path` — Translasi Path

```c
static const char *ENCRYPTED_DIR = "./encrypted_storage";

static void enc_path(char *out, size_t size, const char *fuse_path) {
    snprintf(out, size, "%s%s", ENCRYPTED_DIR, fuse_path);
}

static void enc_file_path(char *out, size_t size, const char *fuse_path) {
    snprintf(out, size, "%s%s%s", ENCRYPTED_DIR, fuse_path, ".enc");
}
```

Dua helper function ini menangani seluruh translasi nama antara dunia `fuse_mount` (nama tampilan, tanpa `.enc`) dan dunia `encrypted_storage` (nama fisik, dengan `.enc`). `enc_path` digunakan untuk direktori, sementara `enc_file_path` digunakan untuk file reguler. Konsistensi penggunaan dua fungsi ini di seluruh operasi FUSE (`getattr`, `readdir`, `open`, `read`, `write`, `truncate`, `unlink`) memastikan tidak ada inkonsistensi nama di antara operasi-operasi tersebut.

### Fungsi `xmp_getattr` — Atribut File/Direktori

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

`getattr` mencoba dua kali: pertama path sebagai direktori (tanpa `.enc`) — jika berhasil langsung kembalikan hasilnya. Jika tidak, coba sebagai file terenkripsi (dengan `.enc`). Ukuran yang dilaporkan ke kernel adalah ukuran file `.enc` di disk, yang identik dengan ukuran data aslinya karena XOR tidak mengubah panjang data. Jika keduanya gagal, kembalikan `-ENOENT`.

### Fungsi `xmp_readdir` — Listing Transparan

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

Saat pengguna menjalankan `ls` pada `fuse_mount`, `readdir` membaca isi `encrypted_storage` dan melakukan transformasi nama secara transparan: setiap entri berakhiran `.enc` ditampilkan tanpa suffix tersebut menggunakan `strncpy` dengan panjang `(namelen - 4)`. Entri lain seperti `.`, `..`, dan subdirektori ditampilkan apa adanya. Hasilnya pengguna melihat nama file bersih tanpa mengetahui adanya file `.enc` di baliknya.

### Fungsi `xmp_read` dan `xmp_write` — Enkripsi/Dekripsi Otomatis

```c
static int xmp_read(const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    int fd = fi->fh ? fi->fh : open(enc_file, O_RDONLY);
    int res = pread(fd, buf, size, offset);
    if (res > 0) xor_buffer(buf, res);   /* dekripsi on-the-fly */
    if (!fi->fh) close(fd);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi) {
    char *enc_buf = malloc(size);
    memcpy(enc_buf, buf, size);
    xor_buffer(enc_buf, size);           /* enkripsi sebelum tulis */
    int res = pwrite(fd, enc_buf, size, offset);
    free(enc_buf);
    return res;
}
```

`xmp_read` membaca data terenkripsi dari file `.enc` menggunakan `pread()`, lalu mendekripsinya dengan `xor_buffer()` sebelum dikembalikan ke kernel. `xmp_write` melakukan kebalikannya: karena buffer dari kernel tidak boleh dimodifikasi, dibuat salinan dengan `malloc()`, disalin dengan `memcpy()`, dienkripsi dengan `xor_buffer()`, baru ditulis ke file `.enc` menggunakan `pwrite()`. Buffer salinan dibebaskan setelah selesai untuk mencegah memory leak.

### `client.c` — TCP Client Mini Database

`client.c` membangun koneksi TCP ke server mini-database di `127.0.0.1:9000` menggunakan socket standar POSIX. Setelah terhubung, program masuk ke loop interaktif yang menampilkan prompt `db >` dan membaca perintah dari stdin. Setiap input dikirim ke server dengan newline sebagai terminator, dan respons server dibaca menggunakan `select()` dengan timeout 10ms untuk mendeteksi akhir respons tanpa terblokir. Perintah `EXIT` dan `QUIT` ditangani secara lokal untuk memutus koneksi dengan bersih.

```c
sockfd = socket(AF_INET, SOCK_STREAM, 0);
server_addr.sin_family = AF_INET;
server_addr.sin_port   = htons(9000);
inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

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

### `Dockerfile` — Kontainerisasi dengan Docker

Dockerfile adalah file teks berisi instruksi untuk membangun sebuah Docker Image. Image bersifat immutable: setiap instruksi membuat layer baru yang di-cache, sehingga build berikutnya lebih cepat jika layer tidak berubah. Docker Image kemudian digunakan untuk membuat Docker Container — instance yang berjalan dari image tersebut.

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

`FROM ubuntu:latest` menetapkan base image. Instruksi `RUN apt-get` menginstal semua dependensi — `build-essential` menyediakan gcc dan toolchain, `libfuse-dev` menyediakan header dan library FUSE — lalu membersihkan cache apt untuk meminimalkan ukuran image. `WORKDIR /app` menetapkan direktori kerja, `COPY . /app` menyalin source code ke dalam image, dua `RUN gcc` mengompilasi `fuse.c` dan `client.c` saat image dibangun, dan `EXPOSE 9000` mendokumentasikan port yang digunakan container. Integrasi dengan FUSE dilakukan saat container dijalankan: `fuse_mount` di host di-bind mount ke `/app/db` di dalam container menggunakan flag `-v $(pwd)/fuse_mount:/app/db`, sehingga semua operasi file server otomatis melewati lapisan enkripsi FUSE.

### Output

**Terminal 1 — Compile dan mount FUSE**

```bash
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ gcc -Wall $(pkg-config fuse --cflags) fuse.c -o fuse $(pkg-config fuse --libs)
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ ./fuse -f fuse_mount
```

Flag `-f` digunakan untuk menjaga FUSE tetap berjalan di foreground sehingga memudahkan debugging dengan `printf`.

**Terminal 2 — Verifikasi enkripsi transparan**

```bash
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ mount | grep fuse_mount
/home/tafidah/SISOP-4-2026-IT-025/soal_2/fuse on /home/tafidah/SISOP-4-2026-IT-025/soal_2/fuse_mount type fuse.fuse (rw,nosuid,nodev,relatime,user_id=1000,group_id=1000)
5 directories, 9 files
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ echo "halo" > fuse_mount/halo.txt
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ cat fuse_mount/halo.txt
halo
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ cat encrypted_storage/halo.txt.enc
␦|
```

Data `"halo\n"` yang ditulis ke `fuse_mount/halo.txt` tersimpan sebagai byte terenkripsi di `encrypted_storage/halo.txt.enc` (karakter `␦|` adalah hasil XOR dari `"halo\n"` dengan kunci `0x76`), namun terbaca normal kembali melalui mount point. Ini membuktikan enkripsi dan dekripsi berjalan secara transparan.

**Build Docker Image**

```bash
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ docker build -t soal-2-modul-4-sisop .
DEPRECATED: The legacy builder is deprecated and will be removed in a future release.
            Install the buildx component to build images with BuildKit:
            https://docs.docker.com/go/buildx/

Sending build context to Docker daemon  61.95kB
Step 1/9 : FROM ubuntu:latest
 ---> f3d28607ddd7
Step 2/9 : RUN apt-get update &&     apt-get install -y         build-essential         libfuse-dev         fuse         pkg-config         ca-certificates     && rm -rf /var/lib/apt/lists/*
 ---> Using cache
 ---> 36b4bca5c3e2
Step 3/9 : WORKDIR /app
 ---> Using cache
 ---> bdaa8523c0d5
Step 4/9 : COPY . /app
 ---> 62c0a18e2d77
Step 5/9 : RUN gcc -Wall $(pkg-config fuse --cflags) fuse.c -o fuse $(pkg-config fuse --libs)
 ---> Running in 2409b53df0a2
 ---> Removed intermediate container 2409b53df0a2
 ---> 9f6742a218a3
Step 6/9 : RUN gcc -Wall client.c -o client
 ---> Running in 6cfc47bbe7c6
 ---> Removed intermediate container 6cfc47bbe7c6
 ---> 9725e903bdf7
Step 7/9 : RUN mkdir -p /app/db
 ---> Running in 457f871e1cc8
 ---> Removed intermediate container 457f871e1cc8
 ---> 143de9dca086
Step 8/9 : EXPOSE 9000
 ---> Running in eec992164362
 ---> Removed intermediate container eec992164362
 ---> 263d074e6079
Step 9/9 : CMD ["./server"]
 ---> Running in d26740b22710
 ---> Removed intermediate container d26740b22710
 ---> 8c51dd989e4c
Successfully built 8c51dd989e4c
Successfully tagged soal-2-modul-4-sisop:latest
```

**Menjalankan container dan menggunakan client**

```bash
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ docker run -d --privileged --name db_app -p 9000:9000 \
    -v $(pwd)/fuse_mount:/app/db soal-2-modul-4-sisop
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ docker ps
CONTAINER ID   IMAGE                  COMMAND      CREATED          STATUS          PORTS                                         NAMES
d4b049c9674d   soal-2-modul-4-sisop   "./server"   12 seconds ago   Up 12 seconds   0.0.0.0:9000->9000/tcp, [::]:9000->9000/tcp   db_app
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ gcc client.c -o client
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ ./client
Connected to DB Server on port 9000
Type HELP for available commands
Type EXIT or QUIT to exit

db > EXIT
Disconnecting...
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ docker stop db_app && docker rm db_app
db_app
db_app
tafidah@DESKTOP-DFFGK1U:~/SISOP-4-2026-IT-025/soal_2$ fusermount -u fuse_mount
```

Seluruh alur integrasi berjalan: FUSE berhasil di-mount dan mengenkripsi file secara transparan, Docker image berhasil dibangun dalam 9 step, container berjalan di background dengan bind mount `fuse_mount` ke `/app/db`, dan client berhasil terhubung ke server di port 9000. Setelah selesai, container dihentikan dan FUSE di-unmount menggunakan `fusermount -u`.

### Kendala

Tidak ditemukan kendala selama pengerjaan.
