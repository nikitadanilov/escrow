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
#include <errno.h>

#include "escrow.h"

enum { PORT = 8087 };

int main(int argc, char **argv) {
        int            sock;
        struct escrow *escrow;
        char           ch;
        int            opt    = 1;
        int32_t        nob    = 0;
        int            result = escrow_init(argv[1], ESCROW_VERBOSE | ESCROW_FORCE, 1, &escrow);
        if (result != 0) {
                errx(EXIT_FAILURE, "escrow_init(): %i", result);
        }
        /*
         * Retrive the socket from the escrow.
         *
         * If this is the first time the server connects to the escrow, this
         * returns -ENOENT and places -1 into sock.
         *
         * If the echo-server is restarted, the escrow returns the socket that
         * the previous instance of the echo-server placed there.
         */
        result = escrow_get(escrow, 0, 0, &sock, &nob, &ch);
        if (result != 0 && result != -ENOENT) {
                errx(EXIT_FAILURE, "escrow_init(): %i", result);
        }
        if (sock == -1) {
                int                server_fd;
                struct sockaddr_in address;
                int                addrlen = sizeof(address);
                if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                        err(EXIT_FAILURE, "socket()");
                }
                if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                        err(EXIT_FAILURE, "setsockopt()");
                }
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = INADDR_ANY;
                address.sin_port = htons(PORT);
                if (bind(server_fd, (struct sockaddr *)&address, sizeof address) < 0) {
                        err(EXIT_FAILURE, "bind()");
                }
                if (listen(server_fd, 3) == -1) {
                        err(EXIT_FAILURE, "listen()");
                }
                if ((sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) == -1) {
                        err(EXIT_FAILURE, "accept()");
                }
                close(server_fd);
                escrow_add(escrow, 0, 0, sock, nob, &ch);
        }
        while ((result = read(sock, &ch, sizeof ch)) > 0) {
                write(sock, &ch, result);
        }
        result = escrow_del(escrow, 0, 0);
        if (result < 0) {
                errx(EXIT_FAILURE, "escrow_del(): %i", result);
        }
        close(sock);
        escrow_fini(escrow);
        return 0;
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
