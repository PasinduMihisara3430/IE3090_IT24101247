#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define AGENT_PORT 9410

int main(int argc, char *argv[])
{
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(AGENT_PORT);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); exit(1);
    }
    printf("Connected to %s:%d\n", ip, AGENT_PORT);

    char line[1024], resp[1024];
    while (fgets(line, sizeof(line), stdin)) {
        send(fd, line, strlen(line), 0);
        ssize_t n = recv(fd, resp, sizeof(resp) - 1, 0);
        if (n <= 0) { printf("Server closed connection\n"); break; }
        resp[n] = '\0';
        printf("%s", resp);
    }
    close(fd);
    return 0;
}
