#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define AGENT_PORT   9410          /* 7000 + 2410 */
#define BACKLOG      10
#define LINE_MAX_LEN 1024

typedef struct {
    int    fd;
    char   buf[4096];   /* receive buffer */
    size_t len;         /* bytes currently in buf */
} conn_t;

/* Reads one line (without '\n') into out. Returns length, or -1 on disconnect/error. */
static int read_line(conn_t *c, char *out, size_t out_size)
{
    while (1) {
        char *nl = memchr(c->buf, '\n', c->len);
        if (nl) {
            size_t line_len = nl - c->buf;
            size_t copy = line_len < out_size - 1 ? line_len : out_size - 1;
            memcpy(out, c->buf, copy);
            out[copy] = '\0';
            if (copy > 0 && out[copy - 1] == '\r') out[copy - 1] = '\0';

            size_t consumed = line_len + 1;
            memmove(c->buf, c->buf + consumed, c->len - consumed);
            c->len -= consumed;
            return (int)copy;
        }

        if (c->len == sizeof(c->buf)) {   /* line too long */
            c->len = 0;
            return -1;
        }

        ssize_t n = recv(c->fd, c->buf + c->len, sizeof(c->buf) - c->len, 0);
        if (n <= 0) return -1;
        c->len += n;
    }
}

static void *client_thread(void *arg)
{
    conn_t c;
    c.fd  = *(int *)arg;
    c.len = 0;
    free(arg);

    char line[LINE_MAX_LEN];
    while (read_line(&c, line, sizeof(line)) >= 0) {
        printf("Got line: [%s]\n", line);
        char reply[LINE_MAX_LEN + 16];
        snprintf(reply, sizeof(reply), "ECHO %s\n", line);
        send(c.fd, reply, strlen(reply), 0);
    }
    close(c.fd);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(AGENT_PORT);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, BACKLOG) < 0) { perror("listen"); exit(1); }

    printf("Agent listening on port %d\n", AGENT_PORT);

    while (1) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int *cfd = malloc(sizeof(int));
        *cfd = accept(listen_fd, (struct sockaddr *)&cli, &len);
        if (*cfd < 0) { perror("accept"); free(cfd); continue; }

        printf("Connection from %s:%d\n",
               inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, cfd);
        pthread_detach(tid);
    }
}
