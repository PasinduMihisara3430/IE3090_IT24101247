#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
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
#define STORAGE_DIR  "./agentfiles/IT24101247"
#define MAX_FILE_SIZE (10L * 1024 * 1024)

typedef struct {
    int    fd;
    char   buf[4096];   /* receive buffer */
    size_t len;         /* bytes currently in buf */
    int    authed;      /* 1 after successful AUTH */
    struct sockaddr_in peer;   /* controller address, used for UDP */
    pthread_t mon_tid;
    volatile int mon_running;  /* 1 while monitor thread is active */
    int    mon_port;
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


/* Fills out with "pid:name,pid:name,..." from ps. Returns 0 on success. */
static int get_proclist(char *out, size_t out_size)
{
        FILE *p = popen("ps -eo pid=,ppid=,comm= | awk '$2!=2 && $1!=2 {print $1, $3}'", "r");
    if (!p) return -1;

    size_t used = 0;
    out[0] = '\0';
    char line[256];
    int first = 1;
    while (fgets(line, sizeof(line), p)) {
        int pid; char name[128];
        if (sscanf(line, "%d %127s", &pid, name) != 2) continue;
        char item[160];
        int n = snprintf(item, sizeof(item), "%s%d:%s", first ? "" : ",", pid, name);
        if (used + n + 1 >= out_size) break;   /* output full: stop */
        memcpy(out + used, item, n);
        used += n;
        out[used] = '\0';
        first = 0;
    }
    pclose(p);
    return 0;
}


/* Whitelisted EXEC: maps an allowed name to a fixed shell command.
 * Returns NULL if the name is not in the whitelist. */
static const char *exec_lookup(const char *name)
{
    if (strcmp(name, "DATE")     == 0) return "date";
    if (strcmp(name, "UPTIME")   == 0) return "uptime -p";
    if (strcmp(name, "DISKFREE") == 0) return "df -h /";
    if (strcmp(name, "HOSTNAME") == 0) return "hostname";
    if (strcmp(name, "WHOAMI")   == 0) return "whoami";
    return NULL;
}

/* Runs a fixed command and squeezes its output into one line. */
static int run_fixed(const char *cmd, char *out, size_t out_size)
{
    FILE *p = popen(cmd, "r");
    if (!p) return -1;

    size_t used = 0;
    char line[256];
    while (fgets(line, sizeof(line), p)) {
        for (char *q = line; *q; q++)
            if (*q == '\n' || *q == '\r') *q = ' ';
        size_t n = strlen(line);
        if (used + n + 1 >= out_size) break;
        memcpy(out + used, line, n);
        used += n;
    }
    while (used > 0 && out[used - 1] == ' ') used--;
    out[used] = '\0';
    pclose(p);
    return 0;
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

/* ---------- File transfer helpers ---------- */

/* Only letters, digits, '.', '_', '-' allowed; no leading '.', so no "../" tricks. */
static int valid_filename(const char *n)
{
    size_t len = strlen(n);
    if (len == 0 || len > 100 || n[0] == '.') return 0;
    for (; *n; n++) {
        unsigned char ch = (unsigned char)*n;
        if (!(isalnum(ch) || ch == '.' || ch == '_' || ch == '-')) return 0;
    }
    return 1;
}

/* Sends all len bytes, looping because send() may send only part. */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

/* Reads exactly `size` bytes (leftover buffer first, then recv) into fp.
 * Returns 0 on success, -1 if the client disconnects early. */
static int receive_to_file(conn_t *c, FILE *fp, long size)
{
    long remaining = size;
    while (remaining > 0) {
        if (c->len > 0) {
            size_t take = c->len < (size_t)remaining ? c->len : (size_t)remaining;
            fwrite(c->buf, 1, take, fp);
            memmove(c->buf, c->buf + take, c->len - take);
            c->len -= take;
            remaining -= (long)take;
        } else {
            char tmp[4096];
            size_t want = remaining < (long)sizeof(tmp) ? (size_t)remaining : sizeof(tmp);
            ssize_t n = recv(c->fd, tmp, want, 0);
            if (n <= 0) return -1;
            fwrite(tmp, 1, n, fp);
            remaining -= n;
        }
    }
    return 0;
}

/* ---------- UDP monitoring ---------- */
#define MONITOR_INTERVAL_SEC 3

static void *monitor_thread(void *arg)
{
    conn_t *c = (conn_t *)arg;
    int us = socket(AF_INET, SOCK_DGRAM, 0);
    if (us < 0) { c->mon_running = 0; return NULL; }

    struct sockaddr_in dst = c->peer;
    dst.sin_port = htons(c->mon_port);

    while (c->mon_running) {
        double cpu; long mem, up;
        get_sysinfo(&cpu, &mem, &up);
        char msg[128];
        int n = snprintf(msg, sizeof(msg), "SYSINFO %.2f %ld %ld SID:%s\n",
                         cpu, mem, up, SID);
        sendto(us, msg, n, 0, (struct sockaddr *)&dst, sizeof(dst));
        for (int i = 0; i < MONITOR_INTERVAL_SEC * 10 && c->mon_running; i++)
            usleep(100000);          /* 0.1s steps so STOP reacts quickly */
    }
    close(us);
    return NULL;
}

static void monitor_stop(conn_t *c)
{
    if (c->mon_running) {
        c->mon_running = 0;
        pthread_join(c->mon_tid, NULL);
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

    if (strcmp(cmd, "LISTPROC") == 0) {
        char procs[LINE_MAX_LEN - 64];
        if (get_proclist(procs, sizeof(procs)) == 0)
            send_response(c, "OK PROCS %s", procs);
        else
            send_response(c, "ERR 007 LISTPROC_FAILED");
        return 1;
    }

    if (strcmp(cmd, "EXEC") == 0) {
        const char *shell_cmd = arg ? exec_lookup(arg) : NULL;
        if (!shell_cmd) {
            send_response(c, "ERR 002 COMMAND_NOT_ALLOWED");
            return 1;
        }
        char output[LINE_MAX_LEN - 64];
        if (run_fixed(shell_cmd, output, sizeof(output)) == 0)
            send_response(c, "OK EXEC_RESULT %s", output);
        else
            send_response(c, "ERR 008 EXEC_FAILED");
        return 1;
    }

    if (strcmp(cmd, "PUT") == 0) {
        char name[128]; long size;
        if (!arg || sscanf(arg, "%127s %ld", name, &size) != 2 ||
            size < 0 || !valid_filename(name)) {
            send_response(c, "ERR 009 BAD_REQUEST");
            return 0;                 /* unknown bytes may follow: close */
        }
        if (size > MAX_FILE_SIZE) {
            send_response(c, "ERR 004 FILE_TOO_LARGE");
            return 0;
        }
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", STORAGE_DIR, name);
        FILE *fp = fopen(path, "wb");
        if (!fp) { send_response(c, "ERR 010 STORE_FAILED"); return 0; }
        int rc = receive_to_file(c, fp, size);
        fclose(fp);
        if (rc < 0) { unlink(path); return 0; }   /* client dropped mid-upload */
        send_response(c, "OK FILE_RECEIVED %s", name);
        return 1;
    }

    if (strcmp(cmd, "GET") == 0) {
        if (!arg || !valid_filename(arg)) {
            send_response(c, "ERR 005 FILE_NOT_FOUND");
            return 1;
        }
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", STORAGE_DIR, arg);
        FILE *fp = fopen(path, "rb");
        if (!fp) { send_response(c, "ERR 005 FILE_NOT_FOUND"); return 1; }
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        rewind(fp);
        send_response(c, "OK FILE_SEND %s %ld", arg, size);
        char tmp[4096];
        long remaining = size;
        while (remaining > 0) {
            size_t want = remaining < (long)sizeof(tmp) ? (size_t)remaining : sizeof(tmp);
            size_t n = fread(tmp, 1, want, fp);
            if (n == 0) break;
            if (send_all(c->fd, tmp, n) < 0) { fclose(fp); return 0; }
            remaining -= (long)n;
        }
        fclose(fp);
        return 1;
    }

    if (strcmp(cmd, "MONITOR") == 0) {
        char sub[16] = ""; int port = 0;
        int cnt = arg ? sscanf(arg, "%15s %d", sub, &port) : 0;
        if (cnt == 2 && strcmp(sub, "START") == 0 && port > 0 && port < 65536) {
            monitor_stop(c);                 /* restart cleanly if already on */
            c->mon_port = port;
            c->mon_running = 1;
            if (pthread_create(&c->mon_tid, NULL, monitor_thread, c) != 0) {
                c->mon_running = 0;
                send_response(c, "ERR 011 MONITOR_FAILED");
            } else {
                send_response(c, "OK MONITOR_STARTED");
            }
        } else if (cnt >= 1 && strcmp(sub, "STOP") == 0) {
            monitor_stop(c);
            send_response(c, "OK MONITOR_STOPPED");
        } else {
            send_response(c, "ERR 009 BAD_REQUEST");
        }
        return 1;
    }

    if (strcmp(cmd, "QUIT") == 0) {
        monitor_stop(c);
        send_response(c, "OK BYE");
        return 0;
    }

    send_response(c, "ERR 006 UNKNOWN_COMMAND");
    return 1;
}

typedef struct {
    int fd;
    struct sockaddr_in peer;
} accept_info_t;

static void *client_thread(void *arg)
{
    accept_info_t *ai = (accept_info_t *)arg;
    conn_t c;
    memset(&c, 0, sizeof(c));
    c.fd   = ai->fd;
    c.peer = ai->peer;
    free(ai);

    char line[LINE_MAX_LEN];
    while (read_line(&c, line, sizeof(line)) >= 0) {
        printf("Got line: [%s]\n", line);
        if (!handle_command(&c, line)) break;
    }
    monitor_stop(&c);                /* also covers ungraceful disconnects */
    close(c.fd);
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    mkdir("./agentfiles", 0755);
    mkdir(STORAGE_DIR, 0755);

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
        accept_info_t *ai = malloc(sizeof(*ai));
        ai->fd = accept(listen_fd, (struct sockaddr *)&cli, &len);
        if (ai->fd < 0) { perror("accept"); free(ai); continue; }
        ai->peer = cli;
        printf("Connection from %s:%d\n",
               inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, ai);
        pthread_detach(tid);
    }
}
