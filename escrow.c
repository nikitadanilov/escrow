/* -*- C -*- */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "escrow.h"

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define SOF(x) ((int32_t)sizeof(x))
#define FORALL(var, limit, ...)                                         \
({                                                                      \
        typeof(limit) __lim;                                            \
        typeof(limit) var;                                              \
        for (__lim = (limit), var = 0; var < __lim && ({ __VA_ARGS__; }); ++var) { \
                ;                                                       \
        }                                                               \
        var == __lim;                                                   \
})
#define EXISTS(var, limit, ...) (!FORALL(var, (limit), !(__VA_ARGS__)))
#define IS0(obj) FORALL(i, sizeof *(obj), ((uint8_t *)obj)[i] == 0)
#define ARRAY_SIZE(a)                           \
({                                              \
        SASSERT(IS_ARRAY(a));                   \
        (int)(sizeof(a) / sizeof(a[0]));        \
})
#define IS_ARRAY(x) (!__builtin_types_compatible_p(typeof(&(x)[0]), typeof(x)))
#define IS_IN(idx, array)                               \
({                                                      \
        SASSERT(IS_ARRAY(array));                       \
        ((unsigned long)(idx)) < ARRAY_SIZE(array);     \
})
#define ASSERT(x) assert(x)
#define SASSERT(cond) _Static_assert((cond), #cond)
#define SET0(obj)                               \
({                                              \
        typeof(obj) __obj = (obj);              \
        SASSERT(!IS_ARRAY(obj));                \
        memset(__obj, 0, sizeof *__obj);        \
})
#define MASK(shift) ((1 << (shift)) - 1)
#define ERROR(x) (x)

#define EV(flags, ...) ({ if(flags & ESCROW_VERBOSE) { __VA_ARGS__; } })

static int32_t min_32(int32_t a, int32_t b) {
        return b + ((a - b) & ((a - b) >> 31));
}

#if 0
static int32_t max_32(int32_t a, int32_t b) {
        return a - ((a - b) & ((a - b) >> 31));
}

static int64_t min_64(int64_t a, int64_t b) {
        return b + ((a - b) & ((a - b) >> 63));
}

static int64_t max_64(int64_t a, int64_t b) {
        return a - ((a - b) & ((a - b) >> 63));
}
#endif

static int send_fd(int socket, int32_t nob, const void *data, int  fd);
static int recv_fd(int socket, int32_t nob,       void *data, int *fd);

static void *mem_alloc(int32_t size);
static void  mem_free(void *mem);

enum {
        ROOT_SHIFT = 10,
        LEAF_SHIFT = 10,
        MAX_IDX    = 1 << (ROOT_SHIFT + LEAF_SHIFT)
};

/* Extensible sequence. */
struct seq {
        void **root[1 << ROOT_SHIFT];
};

static void     seq_init(struct seq *s);
static void     seq_fini(struct seq *s);
static int      seq_add (struct seq *s, int32_t idx, void *val);
static void     seq_del (struct seq *s, int32_t idx);
static void    *seq_get (const struct seq *s, int32_t idx);
static int32_t  seq_nr  (const struct seq *s);

struct tag {
        struct seq seq;
};

struct msg;
struct mrep;

struct escrowd { /* Escrow domain representing one (restartable) client process. */
        int                fd; /* UNIX socket. */
        int            stream; /* Accepted socket. */
        const char      *path;
        uint64_t          key;
        int32_t       nr_tags;
        struct tag      *tags;
        struct msg       *req;
        struct mrep      *rep;
};

struct slot {
        int     fd;
        int     ufd;
        int32_t nob;
        uint8_t data[0];
};

enum {
        QUEUE       = 16,
        MAX_PAYLOAD = 1 << 15,
        MAX_REPLY   = 1 << 10,
        FORK_DELAY  = 1
};

enum opcode {
        HEL,
        ADD,
        DEL,
        REP,
        TAG,
        INF,
        GET
};

struct mhel {
        int16_t opcode;
        int16_t nr_tags;
        int32_t flags;
        int64_t key;
};

struct madd {
        int16_t opcode;
        int16_t tag;
        int32_t idx;
        int32_t ufd;
        int32_t nob;
        uint8_t data[MAX_PAYLOAD];
};

struct mdel {
        int16_t opcode;
        int16_t tag;
        int32_t idx;
};

struct mrep {
        int16_t opcode;
        int16_t rc;
        int16_t nob;
        uint8_t data[MAX_REPLY];
};

struct mtag {
        int16_t opcode;
        int16_t tag;
};

struct minf {
        int16_t opcode;
        int16_t pad;
        int32_t nr;
        int32_t total;
};

struct mget {
        int16_t opcode;
        int16_t tag;
        int32_t idx;
};

struct msg {
        union {
                int16_t opcode;
                struct mhel hel;
                struct madd add;
                struct mdel del;
                struct mrep rep;
                struct mtag tag;
                struct minf inf;
                struct mget get;
        };
};

/* @msg */

static int32_t msize(const struct msg *m) {
        switch (m->opcode) {
        case ADD:
                return offsetof(struct madd, data) + m->add.nob;
        case DEL:
                return sizeof m->del;
        case REP:
                return offsetof(struct mrep, data) + m->rep.nob;
        case TAG:
                return sizeof m->tag;
        case INF:
                return sizeof m->inf;
        case GET:
                return sizeof m->get;
        }
        ASSERT("Wrong opcode.");
        return 0;
}

static void mprint(const struct msg *m) {
        switch (m->opcode) {
        case ADD:
                printf("{ADD %3i %3i %3i %4i}", m->add.tag, m->add.idx, m->add.ufd, m->add.nob);
                break;
        case DEL:
                printf("{DEL %3i %3i}", m->del.tag, m->del.idx);
                break;
        case REP:
                printf("{REP %3i \"%s\"}", m->rep.rc, m->rep.data);
                break;
        case TAG:
                printf("{REP %3i}", m->tag.tag);
                break;
        case INF:
                printf("{INF %4i %5i}", m->inf.nr, m->inf.total);
                break;
        case GET:
                printf("{GET %3i %3i}", m->get.tag, m->get.idx);
                break;
        default:
                printf("{UNKNOWN %i}", m->opcode);
        }
}

static int mrecv(int fd, struct msg *m, int *out) {
        int result;
        SET0(m);
        result = recv_fd(fd, sizeof *m, m, out);
        printf("recv: ");
        mprint(m);
        printf(" (%i) %3i\n", *out, result);
        return result;
}

static int msend(int fd, const struct msg *m, int in) {
        int result = send_fd(fd, msize(m), m, in);
        printf("send: ");
        mprint(m);
        printf(" (%i) %3i\n", in, result);
        return result;
}

static bool m_is_valid(const struct escrowd *d, int16_t tag, int32_t idx, int16_t ufd) {
        return 0 <= tag && tag < d->nr_tags && 0 <= idx && idx < MAX_IDX && ufd >= 0;
}

static int reply(struct escrowd *d, int16_t rc, const char *descr) {
        ASSERT(strlen(descr) + 1 <= ARRAY_SIZE(d->rep->data));
        d->rep->opcode = REP;
        d->rep->rc     = rc;
        d->rep->nob    = strlen(descr) + 1;
        strcpy((void *)d->rep->data, descr);
        return msend(d->stream, (void *)d->rep, -1);
}

static int ok(struct escrowd *d) {
        return reply(d, 0, "");
}

static void slot_fini(struct slot *s) {
        close(s->fd);
        mem_free(s);
}

static int add(struct escrowd *d, const struct madd *m, int fd) {
        struct slot *s;
        int          result;
        ASSERT(m->opcode == ADD);
        ASSERT(fd >= 0);
        if (UNLIKELY(!m_is_valid(d, m->tag, m->idx, m->ufd) || m->nob < 0 || m->nob > MAX_PAYLOAD)) {
                return reply(d, -EINVAL, "Wrong ADD request.");
        }
        s = seq_get(&d->tags[m->tag].seq, m->idx);
        if (s != NULL) {
                slot_fini(s);
        }
        s = mem_alloc(sizeof *s + m->nob);
        if (UNLIKELY(s == NULL)) {
                return reply(d, -ENOMEM, "Cannot allocate a slot.");
        }
        s->fd  = fd;
        s->ufd = m->ufd;
        s->nob = m->nob;
        memcpy(&s->data, &m->data, m->nob);
        result = seq_add(&d->tags[m->tag].seq, m->idx, s);
        if (result != 0) {
                return reply(d, result, "Cannot extend a sequence.");
        }
        return ok(d);
}

static int del(struct escrowd *d, const struct mdel *m, int fd) {
        struct slot *s;
        ASSERT(m->opcode == DEL);
        if (UNLIKELY(!m_is_valid(d, m->tag, m->idx, 0))) {
                return reply(d, -EINVAL, "Wrong DEL request.");
        }
        if (fd != -1) {
                return reply(d, -EINVAL, "Descriptor present in a DEL request.");
        }
        s = seq_get(&d->tags[m->tag].seq, m->idx);
        if (UNLIKELY(s == NULL)) {
                return reply(d, -EINVAL, "Non-existent index in DEL request.");
        }
        seq_del(&d->tags[m->tag].seq, m->idx);
        slot_fini(s);
        return ok(d);
}

static int tag(struct escrowd *d, const struct mtag *m, int fd) {
        struct tag *t    = &d->tags[m->tag];
        struct minf info = {};
        int32_t     max;
        ASSERT(m->opcode == TAG);
        if (UNLIKELY(!m_is_valid(d, m->tag, 0, 0))) {
                return reply(d, -EINVAL, "Wrong TAG request.");
        }
        if (fd != -1) {
                return reply(d, -EINVAL, "Descriptor present in a TAG request.");
        }
        max = seq_nr(&t->seq);
        info.opcode = INF;
        for (int32_t i = 0; i < max; ++i) {
                struct slot *s = seq_get(&t->seq, i);
                if (s != NULL) {
                        ++info.nr;
                        info.total += s->nob;
                }
        }
        return msend(d->stream, (void *)&info, -1);
}

static int get(struct escrowd *d, const struct mget *m, int fd) {
        struct slot *s;
        struct madd  add;
        ASSERT(m->opcode == GET);
        if (UNLIKELY(!m_is_valid(d, m->tag, m->idx, 0))) {
                return reply(d, -EINVAL, "Wrong DEL request.");
        }
        if (fd != -1) {
                return reply(d, -EINVAL, "Descriptor present in a GET request.");
        }
        s = seq_get(&d->tags[m->tag].seq, m->idx);
        if (UNLIKELY(s == NULL)) {
                return reply(d, -ENOENT, "Non-existent index in a GET request.");
        }
        add.opcode = ADD;
        add.tag    = m->tag;
        add.idx    = m->idx;
        add.ufd    = s->ufd;
        add.nob    = s->nob;
        memcpy(add.data, s->data, s->nob);
        return msend(d->stream, (void *)&add, s->fd);
}

/* @daemon */

int escrowd_init(struct escrowd **out, const char *path, uint32_t flags, int32_t nr_tags) {
        struct sockaddr_un address;
        int                result;
        mode_t             mask;
        struct tag        *tags = mem_alloc(nr_tags * sizeof tags[0]);
        struct escrowd    *d    = mem_alloc(sizeof *d);
        if (d == NULL || tags == NULL) {
                mem_free(d);
                mem_free(tags);
                warn("Cannot allocate escrowd.");
                return ERROR(-ENOMEM);
        }
        if (flags & ESCROW_FORCE) {
                unlink(path);
        }
        if (strlen(path) >= sizeof(address.sun_path) - 1) {
                warn("path is too long: \"%s\"", path);
                return ERROR(-EINVAL);
        }
        if ((d->fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
                warn("socket()");
                return ERROR(-errno);
        }
        address.sun_family = AF_UNIX;
        strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
        mask = umask(0777 & ~(S_IRUSR | S_IWUSR)); /* rw------- */
        result = bind(d->fd, (struct sockaddr *)&address, sizeof(address));
        umask(mask);
        if (result < 0) {
                warn("bind()");
                return ERROR(-errno);
        }
        if (listen(d->fd, QUEUE) < 0) {
                warn("listen()");
                return ERROR(-errno);
        }
        printf("Listening on \"%s\"\n", path);
        d->tags = tags;
        d->path = path;
        d->nr_tags = nr_tags;
        for (int32_t i = 0; i < nr_tags; ++i) {
                seq_init(&d->tags[i].seq);
        }
        *out = d;
        return 0;
}

void escrowd_fini(struct escrowd *d) {
        for (int32_t i = 0; i < d->nr_tags; ++i) {
                struct seq *s   = &d->tags[i].seq;
                int32_t     max = seq_nr(s);
                for (int32_t j = 0; j < max; ++j) {
                        mem_free(seq_get(s, i));
                }
                seq_fini(&d->tags[i].seq);
        }
        mem_free(d->tags);
        close(d->stream);
        close(d->fd);
        unlink(d->path);
}

int escrowd_loop(struct escrowd *d) {
        struct msg  m   = {};
        struct mrep rep = {};
        int         fd;
        int         result;
        d->req = &m;
        d->rep = &rep;
        d->stream = accept(d->fd, NULL, NULL);
        if (d->stream < 0) {
                return -errno;
        }
        while (true) {
                fd = -1;
                result = mrecv(d->stream, &m, &fd);
                if (result != 0) {
                        break;
                }
                switch (m.opcode) {
                case ADD:
                        result = add(d, &m.add, fd);
                        break;
                case DEL:
                        result = del(d, &m.del, fd);
                        break;
                case TAG:
                        result = tag(d, &m.tag, fd);
                        break;
                case GET:
                        result = get(d, &m.get, fd);
                        break;
                default:
                        result = reply(d, -EPROTO, "Unexpected message type.");
                }
                if (result != 0) {
                        break;
                }
        }
        close(fd);
        close(d->stream);
        return result;
}

int escrowd(const char *path, uint32_t flags, int32_t nr_tags) {
        struct escrowd *d;
        int             result = escrowd_init(&d, path, flags, nr_tags);
        if (result != 0) {
                errx(EXIT_FAILURE, "escrowd_init(): %i", result);
        }
        while (true) {
                int result = escrowd_loop(d);
                EV(flags, fprintf(stderr, "Session completed with %i.\n", result));
        }
        escrowd_fini(d);
        return 0;
}

int escrowd_fork(const char *path, uint32_t flags, int32_t nr_tags) {
        int result = fork();
        if (result == 0) {
                result = daemon(true, true) ?: prctl(PR_SET_NAME, "escrowd", 0, 0, 0);
                if (result == 0) {
                        result = escrowd(path, flags, nr_tags);
                } else {
                        result = -errno;
                }
        } else if (result > 0) {
                sleep(FORK_DELAY);
                result = 0;
        }
        return result;
}

/* @seq  */

static void seq_init(struct seq *s) {
        ASSERT(IS0(s));
}

static void seq_fini(struct seq *s) {
        for (int32_t i = 0; i < ARRAY_SIZE(s->root); ++i) {
                mem_free(s->root[i]);
        }
}

static int seq_add(struct seq *s, int32_t idx, void *val) {
        int32_t rix = idx >> LEAF_SHIFT;
        ASSERT(idx < MAX_IDX);
        if (UNLIKELY(s->root[rix] == NULL)) {
                s->root[rix] = mem_alloc(sizeof(uint64_t) << LEAF_SHIFT);
                if (UNLIKELY(s->root[rix] == NULL)) {
                        return ERROR(-ENOMEM);
                }
        }
        s->root[rix][idx & MASK(LEAF_SHIFT)] = val;
        return 0;
}

static void seq_del(struct seq *s, int32_t idx) {
        int32_t rix = idx >> LEAF_SHIFT;
        ASSERT(idx < MAX_IDX);
        if (LIKELY(s->root[rix] != NULL)) {
                s->root[rix][idx & MASK(LEAF_SHIFT)] = 0;
        }
}

static void *seq_get(const struct seq *s, int32_t idx) {
        int32_t rix = idx >> LEAF_SHIFT;
        ASSERT(idx < MAX_IDX);
        if (LIKELY(s->root[rix] != NULL)) {
                return s->root[rix][idx & MASK(LEAF_SHIFT)];
        } else {
                return NULL;
        }
}

static int32_t seq_nr(const struct seq *s) {
        for (int32_t rix = ARRAY_SIZE(s->root) - 1; rix >= 0; --rix) {
                if (s->root[rix] != NULL) {
                        for (int32_t lix = (1 << LEAF_SHIFT) - 1; lix >= 0; --lix) {
                                if (s->root[rix][lix] != 0) {
                                        return (rix << ROOT_SHIFT) + lix + 1;
                                }
                        }
                }
        }
        return 0;
}

union ctrl {
        char           buf[CMSG_SPACE(sizeof (int))];
        struct cmsghdr hdr;
};

static int send_fd(int socket, int32_t nob, const void *data, int fd) {
        struct iovec    iov;
        struct msghdr   msgh;
        union ctrl      cmsg;
        msgh.msg_name    = NULL;
        msgh.msg_namelen = 0;
        msgh.msg_iov     = &iov;
        msgh.msg_iovlen  = 1;
        iov.iov_base     = (void *)data;
        iov.iov_len      = nob;
        if (fd >= 0) {
                struct cmsghdr *cmsgp;
                msgh.msg_control    = cmsg.buf;
                msgh.msg_controllen = sizeof cmsg.buf;
                cmsgp = CMSG_FIRSTHDR(&msgh);
                cmsgp->cmsg_level = SOL_SOCKET;
                cmsgp->cmsg_type  = SCM_RIGHTS;
                cmsgp->cmsg_len   = CMSG_LEN(sizeof fd);
                memcpy(CMSG_DATA(cmsgp), &fd, sizeof fd);
        } else {
                msgh.msg_control    = NULL;
                msgh.msg_controllen = 0;
        }
        if (sendmsg(socket, &msgh, 0) == -1) {
                return -errno;
        }
        return 0;
}

static int recv_fd(int socket, int32_t nob, void *data, int *fd) {
        ssize_t         nr;
        struct iovec    iov;
        struct msghdr   msgh;
        union ctrl      cmsg;
        struct cmsghdr *cmsgp;

        *fd                 = -1;
        msgh.msg_name       = NULL;
        msgh.msg_namelen    = 0;
        msgh.msg_iov        = &iov;
        msgh.msg_iovlen     = 1;
        iov.iov_base        = data;
        iov.iov_len         = nob;
        msgh.msg_control    = cmsg.buf;
        msgh.msg_controllen = sizeof cmsg.buf;
        nr = recvmsg(socket, &msgh, 0);
        if (nr == -1) {
                return -errno;
        } else if (nr == 0) {
                return -ESHUTDOWN;
        }
        cmsgp = CMSG_FIRSTHDR(&msgh);
        if (cmsgp == NULL) {
                return 0;
        }
        if (cmsgp->cmsg_len != CMSG_LEN(sizeof *fd) ||
            cmsgp->cmsg_level != SOL_SOCKET || cmsgp->cmsg_type != SCM_RIGHTS) {
                return -EPROTO;
        }
        memcpy(fd, CMSG_DATA(cmsgp), sizeof *fd);
        return 0;
}

static void *mem_alloc(int32_t size) {
        return calloc(1, size);
}

static void mem_free(void *mem) {
        free(mem);
}

/* @client */

struct escrow {
        int      fd;
        uint32_t flags;
};

static int replied(const struct escrow *e, const struct msg *m) {
        if (m->opcode == REP) {
                if (m->rep.rc != 0) {
                        EV(e->flags, fprintf(stderr, "Received from the escrowd: %i \"%s\"\n",
                                             m->rep.rc, m->rep.data));
                }
                return m->rep.rc;
        } else {
                EV(e->flags, fprintf(stderr, "Unexpected reply from escrowd: %i\n", m->opcode));
                return -EPROTO;
        }
}

static int escrow_init_try(const char *path, uint32_t flags, int32_t nr_tags, struct escrow **escrow) {
        struct escrow *e = mem_alloc(sizeof *e);
        int            result;
        if (LIKELY(e != NULL)) {
                if (path == NULL) {
                        path = getenv("ESCROW_PATH");
                }
                e->flags = flags;
                if ((e->fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0) {
                        struct sockaddr_un address;
                        address.sun_family = AF_UNIX;
                        strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
                        if (connect(e->fd, (void *)&address, sizeof address) >= 0) {
                                EV(e->flags, fprintf(stderr, "Connected to \"%s\"\n", path));
                                *escrow = e;
                                return 0;
                        } else if (errno == ENOENT || errno == ECONNREFUSED || errno == ESHUTDOWN) {
                                EV(e->flags, fprintf(stderr, "Starting escrowd (%i).\n", errno));
                                result = escrowd_fork(path, flags, nr_tags);  /* Nobody is there. */
                                if (result == 0) { /* Re-try. */
                                        result = -EAGAIN;
                                }
                        } else {
                                EV(e->flags, warn("connect()"));
                                result = -errno;
                        }
                        if (result != 0) {
                                close(e->fd);
                        }
                } else {
                        EV(e->flags, warn("socket()"));
                        result = -errno;
                }
                if (result != 0) {
                        mem_free(e);
                }
        } else {
                result = -ENOMEM;
        }
        return result;
}

int escrow_init(const char *path, uint32_t flags, int32_t nr_tags, struct escrow **escrow) {
        int result;
        do {
                result = escrow_init_try(path, flags, nr_tags, escrow);
        } while (result == -EAGAIN);
        return result;
}

void escrow_fini(struct escrow *escrow) {
        close(escrow->fd);
        mem_free(escrow);
}

int escrow_tag(struct escrow *escrow, int16_t tag, int32_t *nr, int32_t *nob) {
        struct msg m = { .tag = { .opcode = TAG, .tag = tag } };
        int        dummy;
        int        result = msend(escrow->fd, &m, -1) ?: mrecv(escrow->fd, &m, &dummy);
        if (result == 0) {
                if (m.opcode == INF) {
                        *nr  = m.inf.nr;
                        *nob = m.inf.total;
                } else {
                        result = replied(escrow, &m);
                }
        }
        return result;
}

int escrow_get(struct escrow *escrow, int16_t tag, int32_t idx, int *fd, int32_t *nob, void *data) {
        struct msg m = { .get = { .opcode = GET, .tag = tag, .idx = idx } };
        int        result = msend(escrow->fd, &m, -1) ?: mrecv(escrow->fd, &m, fd);
        if (result == 0) {
                if (m.opcode == ADD) {
                        memcpy(data, m.add.data, min_32(*nob, m.add.nob));
                        *nob = m.add.nob;
                } else {
                        result = replied(escrow, &m);
                }
        }
        return result;
}

int escrow_add(struct escrow *escrow, int16_t tag, int32_t idx, int fd, int32_t nob, void *data) {
        struct msg m = { .add = { .opcode = ADD, .tag = tag, .idx = idx, .ufd = fd, .nob = nob } };
        int        dummy;
        ASSERT(nob <= ARRAY_SIZE(m.add.data));
        memcpy(m.add.data, data, nob);
        return msend(escrow->fd, &m, fd) ?: mrecv(escrow->fd, &m, &dummy) ?: replied(escrow, &m);
}

int escrow_del(struct escrow *escrow, int16_t tag, int32_t idx) {
        struct msg m = { .del = { .opcode = DEL, .tag = tag, .idx = idx } };
        int        dummy;
        return msend(escrow->fd, &m, -1) ?: mrecv(escrow->fd, &m, &dummy) ?: replied(escrow, &m);
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
