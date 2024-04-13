/* -*- C -*- */
/* Copyright 2024 Nikita Danilov <danilov@gmail.com> */
/* See https://github.com/nikitadanilov/escrow/blob/master/LICENCE for the licencing information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <err.h>
#include <arpa/inet.h>
#include <stdbool.h>

enum { PORT = 8087 };

int main(int argc, char **argv) {
        int                sock;
        struct sockaddr_in serv_addr;
        const char         cycle[] = "0123456789abcdefghijklmnopqrstuvwxyz";

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                err(EXIT_FAILURE, "socket()");
        }
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);
        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
                err(EXIT_FAILURE, "inet_pton()");
        }
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                err(EXIT_FAILURE, "connect()");
        }
        for (int i = 0; true; ++i) {
                int ch = cycle[i % sizeof cycle];
                ssize_t rc = write(sock, &ch, 1);
                char back;
                if (rc != 1) {
                        err(EXIT_FAILURE, "write");
                }
                rc = read(sock, &back, 1);
                if (rc != 1) {
                        err(EXIT_FAILURE, "read");
                }
                if (back != ch) {
                        errx(EXIT_FAILURE, "mismatch: %c != %c", ch, back);
                }
        }
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  scroll-step: 1
 *  indent-tabs-mode: nil
 *  End:
 */
