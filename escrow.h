/* -*- C -*- */
/* Copyright 2024 Nikita Danilov <danilov@gmail.com> */
/* See https://github.com/nikitadanilov/escrow/blob/master/LICENCE for the licencing information. */

#ifndef __LIBESCROW_H__
#define __LIBESCROW_H__

#include <stdint.h>

struct escrow;

enum {
        ESCROW_CREAT   = 1 << 0,
        ESCROW_VERBOSE = 1 << 1,
        ESCROW_FORCE   = 1 << 2
};

int  escrow_init(const char *path, uint32_t flags, int32_t nr_tags, struct escrow **escrow);
void escrow_fini(struct escrow *escrow);

int escrow_tag(struct escrow *escrow, int16_t tag, int32_t *nr, int32_t *nob);
int escrow_get(struct escrow *escrow, int16_t tag, int32_t idx, int *fd, int32_t *nob, void *data);
int escrow_add(struct escrow *escrow, int16_t tag, int32_t idx, int  fd, int32_t  nob, void *data);
int escrow_del(struct escrow *escrow, int16_t tag, int32_t idx);

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  scroll-step: 1
 *  indent-tabs-mode: nil
 *  End:
 */
