#include "server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int run_server(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(fd, 16) < 0) {
        perror("listen");
        return 1;
    }

    printf("listening on port %d\n", port);

    for (;;) {

        int client_fd = accept(fd, NULL, NULL);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("got a client, fd = %d\n", client_fd);
        send(client_fd, "connection successful\n",
             strlen("connection successful\n"), 0);

        char buf[1024];
        ssize_t n;

        while ((n = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
            printf("received %zd bytes: %.*s\n", n, (int)n, buf);
            send(client_fd, buf, (size_t)n, 0);
        }

        if (n < 0) {
            perror("recv");
        }

        close(client_fd);
    }

    return 0;
}
