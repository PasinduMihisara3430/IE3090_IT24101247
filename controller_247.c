#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define AGENT_PORT 9410
#define LINE_LEN   2048

static int  sock_fd;
static char rbuf[8192];
static size_t rlen = 0;

/* Reads one line from the socket (without '\n'). Returns -1 on disconnect. */
static int read_line(char *out, size_t out_size)
{
    while (1) {
        char *nl = memchr(rbuf, '\n', rlen);
        if (nl) {
            size_t ll = nl - rbuf;
            size_t copy = ll < out_size - 1 ? ll : out_size - 1;
            memcpy(out, rbuf, copy);
            out[copy] = '\0';
            memmove(rbuf, rbuf + ll + 1, rlen - ll - 1);
            rlen -= ll + 1;
            return (int)copy;
        }
        if (rlen == sizeof(rbuf)) rlen = 0;
        ssize_t n = recv(sock_fd, rbuf + rlen, sizeof(rbuf) - rlen, 0);
        if (n <= 0) return -1;
        rlen += n;
    }
}

static int send_all(const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock_fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

/* PUT <localfile> : uploads using the local file's name */
static void do_put(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("Cannot open %s\n", path); return; }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    char hdr[512];
    snprintf(hdr, sizeof(hdr), "PUT %s %ld\n", name, size);
    send_all(hdr, strlen(hdr));

    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), fp)) > 0)
        if (send_all(tmp, n) < 0) break;
    fclose(fp);

    char line[LINE_LEN];
    if (read_line(line, sizeof(line)) >= 0) printf("%s\n", line);
}

/* GET <name> : saves into ./downloads/<name> */
static void do_get(const char *name)
{
    char hdr[512];
    snprintf(hdr, sizeof(hdr), "GET %s\n", name);
    send_all(hdr, strlen(hdr));

    char line[LINE_LEN];
    if (read_line(line, sizeof(line)) < 0) return;
    printf("%s\n", line);

    char fname[256]; long size;
    if (sscanf(line, "OK FILE_SEND %255s %ld", fname, &size) != 2) return;

    mkdir("downloads", 0755);
    char out[512];
    snprintf(out, sizeof(out), "downloads/%s", fname);
    FILE *fp = fopen(out, "wb");
    if (!fp) { printf("Cannot write %s\n", out); return; }

    long remaining = size;
    while (remaining > 0) {
        if (rlen > 0) {
            size_t take = rlen < (size_t)remaining ? rlen : (size_t)remaining;
            fwrite(rbuf, 1, take, fp);
            memmove(rbuf, rbuf + take, rlen - take);
            rlen -= take;
            remaining -= (long)take;
        } else {
            char tmp[4096];
            size_t want = remaining < (long)sizeof(tmp) ? (size_t)remaining : sizeof(tmp);
            ssize_t n = recv(sock_fd, tmp, want, 0);
            if (n <= 0) break;
            fwrite(tmp, 1, n, fp);
            remaining -= n;
        }
    }
    fclose(fp);
    printf("Saved %s (%ld bytes)\n", out, size - remaining);
}

int main(int argc, char *argv[])
{
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(AGENT_PORT);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); exit(1);
    }
    printf("Connected to %s:%d\n", ip, AGENT_PORT);
    printf("Commands: AUTH <token>, SYSINFO, LISTPROC, EXEC <name>, PUT <localfile>, GET <name>, QUIT\n");

    char input[LINE_LEN], line[LINE_LEN];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\r\n")] = '\0';
        if (input[0] == '\0') continue;

        if (strncmp(input, "PUT ", 4) == 0) { do_put(input + 4); continue; }
        if (strncmp(input, "GET ", 4) == 0) { do_get(input + 4); continue; }

        strcat(input, "\n");
        if (send_all(input, strlen(input)) < 0) { printf("Send failed\n"); break; }
        if (read_line(line, sizeof(line)) < 0) { printf("Server closed connection\n"); break; }
        printf("%s\n", line);
        if (strncmp(input, "QUIT", 4) == 0) break;
    }
    close(sock_fd);
    return 0;
}
