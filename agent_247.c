#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define AGENT_PORT   9410          /* 7000 + 2410 */
#define SID          "7421"        /* 1247 reversed */
#define AUTH_TOKEN   "OPS-1247"
#define BACKLOG      10
#define LINE_MAX_LEN 1024

typedef struct {
    int    fd;
    char   buf[4096];   /* receive buffer */
    size_t len;         /* bytes currently in buf */
    int    authed;      /* 1 after successful AUTH */
} conn_t;


/* ---------- System information helpers ---------- */

/* Reads CPU load (1-min loadavg), used memory in MB, and uptime in seconds. */
static void get_sysinfo(double *cpu, long *mem_used_mb, long *uptime)
{
    *cpu = 0.0; *mem_used_mb = 0; *uptime = 0;

    FILE *f = fopen("/proc/loadavg", "r");
    if (f) { if (fscanf(f, "%lf", cpu) != 1) *cpu = 0.0; fclose(f); }

    f = fopen("/proc/uptime", "r");
    if (f) {
        double up;
        if (fscanf(f, "%lf", &up) == 1) *uptime = (long)up;
        fclose(f);
    }

    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64]; long val; char unit[16];
        long total = 0, avail = 0;
        while (fscanf(f, "%63s %ld %15s", key, &val, unit) >= 2) {
            if (strcmp(key, "MemTotal:") == 0) total = val;
            else if (strcmp(key, "MemAvailable:") == 0) { avail = val; break; }
        }
        fclose(f);
        *mem_used_mb = (total - avail) / 1024;
    }
}

/* Sends one response line, always ending with " SID:<sid>\n". */
static void send_response(conn_t *c, const char *fmt, ...)
{
    char msg[LINE_MAX_LEN + 64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg) - 32, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n > sizeof(msg) - 32) n = sizeof(msg) - 32;
    n += snprintf(msg + n, 32, " SID:%s\n", SID);
    send(c->fd, msg, n, 0);
}

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

/* Handles one command line. Returns 1 to keep going, 0 to close connection. */
static int handle_command(conn_t *c, char *line)
{
    char *cmd = strtok(line, " ");
    if (!cmd) return 1;                       /* empty line: ignore */
    char *arg = strtok(NULL, "");             /* rest of line */

    if (strcmp(cmd, "AUTH") == 0) {
        if (arg && strcmp(arg, AUTH_TOKEN) == 0) {
            c->authed = 1;
            send_response(c, "OK AUTHENTICATED");
        } else {
            send_response(c, "ERR 001 AUTH_FAILED");
        }
        return 1;
    }

    if (!c->authed) {
        send_response(c, "ERR 003 NOT_AUTHENTICATED");
        return 1;
    }

    if (strcmp(cmd, "SYSINFO") == 0) {
        double cpu; long mem, up;
        get_sysinfo(&cpu, &mem, &up);
        send_response(c, "OK SYSINFO %.2f %ld %ld", cpu, mem, up);
        return 1;
    }

    if (strcmp(cmd, "QUIT") == 0) {
        send_response(c, "OK BYE");
        return 0;
    }

    send_response(c, "ERR 006 UNKNOWN_COMMAND");
    return 1;
}

static void *client_thread(void *arg)
{
    conn_t c;
    c.fd     = *(int *)arg;
    c.len    = 0;
    c.authed = 0;
    free(arg);

    char line[LINE_MAX_LEN];
    while (read_line(&c, line, sizeof(line)) >= 0) {
        printf("Got line: [%s]\n", line);
        if (!handle_command(&c, line)) break;
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
