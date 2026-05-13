# Dockerfile - Soal 2: Poke MOO
FROM ubuntu:latest

# Install dependensi: libfuse dan build tools
RUN apt-get update && \
    apt-get install -y \
        build-essential \
        libfuse-dev \
        fuse \
        pkg-config \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Salin seluruh source code ke dalam image
COPY . /app

# Compile fuse.c
# Menghapus '|| true' agar build gagal jika ada error (lebih aman)
RUN gcc -Wall $(pkg-config fuse --cflags) fuse.c -o fuse $(pkg-config fuse --libs)

# Compile client.c
RUN gcc -Wall client.c -o client

# Buat direktori database agar server bisa menulis di sana
RUN mkdir -p /app/db

# Expose port server
EXPOSE 9000

# Jalankan server
# Pastikan file binary 'server' memiliki izin eksekusi (chmod +x) di host sebelum di-copy
CMD ["./server"]
