#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define AGENT_PORT 9410          /* 7000 + 2410 */
#define BACKLOG    10

static void *client_thread(void *arg)
{
    int fd = *(int *)arg;
    free(arg);

    char buf[1024];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
        send(fd, buf, n, 0);          /* echo for now */
    }
    close(fd);
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
