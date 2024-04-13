/* -*- C -*- */

#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "escrow.h"

int escrowd(const char *path, uint32_t flags, int32_t nr_tags);

enum { NR_TAGS = 32 };

static void usage() {
        fprintf(stderr,
                "    Usage: escrowd OPTIONS path-to-socket\n\n"
                "    Where possible OPTIONS are\n\n"
                "        -d           Daemonise (otherwise runs in foreground).\n"
                "        -v           Make the daemon verbose.\n"
                "        -f           Force re-creation of the socket if it already exists.\n"
                "        -t nr_tags   Set the number of tags (default: %i).\n"
                "        -h           Dsiplay this help message.\n\n",
                NR_TAGS);
        exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
        int      opt;
        uint32_t flags     = 0;
        int32_t  nr_tags   = 32;
        bool     daemonise = false;
        while ((opt = getopt(argc, argv, "hdfvt:")) != -1) {
                switch (opt) {
                case 'd':
                        daemonise = true;
                        break;
                case 'f':
                        flags |= ESCROW_FORCE;
                        break;
                case 'v':
                        flags |= ESCROW_VERBOSE;
                        break;
                case 't':
                        nr_tags = atoi(optarg);
                        break;
                case 'h':
                default:
                        usage();
                }
        }
        if (optind >= argc) {
                fprintf(stderr, "    Path to a UNIX domain socket must follow the options.\n");
                usage();
        }
        if (daemonise && daemon(true, true) != 0) {
                err(EXIT_FAILURE, "daemon");
        }
        return escrowd(argv[optind], flags, nr_tags);
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
