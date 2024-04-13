/* -*- C -*- */

#include "escrow.h"

int escrowd(const char *path, uint32_t flags, int32_t nr_tags);

int main(int argc, char **argv) {
        return escrowd(argv[1], ESCROW_VERBOSE, 32);
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
