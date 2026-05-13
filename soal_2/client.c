#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 9000
#define BUF_SIZE    65536

int main(void)
{
    int sockfd;
    struct sockaddr_in server_addr;
    char send_buf[BUF_SIZE];
    char recv_buf[BUF_SIZE];
    int n;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to DB Server on port %d\n", SERVER_PORT);
    printf("Type HELP for available commands\n");
    printf("Type EXIT or QUIT to exit\n\n");

    memset(recv_buf, 0, sizeof(recv_buf));
    n = recv(sockfd, recv_buf, sizeof(recv_buf) - 1, MSG_DONTWAIT);
    if (n > 0) {
        recv_buf[n] = '\0';
        printf("%s", recv_buf);
    }

    while (1) {
        printf("db > ");
        fflush(stdout);

        if (fgets(send_buf, sizeof(send_buf), stdin) == NULL) {
            break;
        }

        size_t len = strlen(send_buf);
        if (len > 0 && send_buf[len - 1] == '\n') {
            send_buf[len - 1] = '\0';
            len--;
        }

        if (strcasecmp(send_buf, "EXIT") == 0 ||
            strcasecmp(send_buf, "QUIT") == 0) {
            printf("Disconnecting...\n");
            break;
        }

        if (len == 0) continue;

        send_buf[len]     = '\n';
        send_buf[len + 1] = '\0';
        len++;

        ssize_t sent = send(sockfd, send_buf, len, 0);
        if (sent < 0) {
            perror("send");
            break;
        }

        memset(recv_buf, 0, sizeof(recv_buf));

        ssize_t total = 0;
        while (1) {
            n = recv(sockfd, recv_buf + total,
                     sizeof(recv_buf) - 1 - total, 0);
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    break;
                }
                if (n == 0) {
                    printf("\nServer disconnected.\n");
                    goto done;
                }
                perror("recv");
                goto done;
            }
            total += n;

            if (total >= (ssize_t)(sizeof(recv_buf) - 1)) break;

            fd_set fds;
            struct timeval tv = {0, 10000};
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);
            int ready = select(sockfd + 1, &fds, NULL, NULL, &tv);
            if (ready <= 0) break;
        }

        recv_buf[total] = '\0';
        if (total > 0) {
            printf("%s\n", recv_buf);
        }
    }

done:
    close(sockfd);
    return 0;
}
