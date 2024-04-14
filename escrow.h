/* -*- C -*- */
/* Copyright 2024 Nikita Danilov <danilov@gmail.com> */
/* See https://github.com/nikitadanilov/escrow/blob/master/LICENCE for the licencing information. */

#ifndef __LIBESCROW_H__
#define __LIBESCROW_H__

/*
 * File descriptor escrow library.
 *
 * This library provides an interface to send process' file descriptors to a
 * separate process ("escrow daemon", "escrowd"). The descriptors can be
 * retrieved later by the sender process or by another process.
 *
 * The motivating use case for the library is zero-downtime service upgrade:
 * consider a network service that maintains socket connections from multiple
 * clients. To upgrade the service to a new version:
 *
 *     - bring it to a "quiescent state" that is, pause accepting new client
 *       connections and new requests from the existing connections, and
 *       complete all ongoing requests;
 *
 *     - place all the sockets in the escrow;
 *
 *     - exit the service process;
 *
 *     - start the new version;
 *
 *     - retrieve all sockets from the escrow;
 *
 *     - resume request and connection processing.
 *
 * From the client perspective this process is transparent (save for a delay):
 * the connection to the server is not broken. Note that escrowd is
 * single-threaded and can have at most a single client at a time. Hence, the
 * new service version binary can start before the previous instance terminated:
 * it will be safely blocked in an attempt to connect to escrowd until the
 * previous instance disconnects.
 *
 * The same mechanism can be used for recovery after a process crash, except in
 * this case there is no guarantee that the connections were left in some known
 * state, and the recovery code needs to figure out how to proceed.
 *
 * An escrow can also be used to provide access to "restricted" file
 * descriptors: a priviledged process can open a device or establish and
 * authenticate a connection and then place the resulting file descriptor in an
 * escrow, from which it can be retrieved by any properly authorised process.
 *
 * One can imagine load-balancing by passing live client connections between
 * multiple instances of a service perhaps across container boundaries.
 *
 * INTERFACE OVERVIEW
 *
 * An escrow connection is established by calling escrow_init(). A parameter of
 * this function is the path to a UNIX domain socket used for the communication
 * with the escrowd. Access to the escrow is authorised by the usual access
 * rules for this pathname.
 *
 * An escrowd process listening on the socket can be started explictly in
 * advance. Alternatively, when escrow_init(), called with ESCROW_CREAT flag,
 * determines that nobody is listening on the socket or the socket does not
 * exist, it starts the daemon automatically.
 *
 * When a file descriptor is placed in an escrow, the user specifies a 16-bit
 * tag and a 32-bit index within a tag. Tags can be used to simplify descriptor
 * recovery. For example, in the service upgrade scenario described above, the
 * service can place all listener sockets in one tag and all accepted stream
 * sockets in another. The recovery can first recover all listeners and then all
 * streams. The total number of tags is specified when the escrow is created.
 *
 * In addition to the tag and the index, a file descriptor has an optional
 * "payload" of up to 32KB. The payload is stored in and retrieved from the
 * escrow together with the file descriptor. In fact, it is possible to store
 * and retrieve payload only without a file descriptor, by providing (-1) as FD
 * argument to escrow_add(). This allows to use escrowd as a simple memory-only
 * data-base. A typical use of payload is to store auxiliary information about
 * the file descriptor that allows recovery.
 *
 * RETURN VALUES
 *
 * All functions return 0 on success, a negated errno value on a failure.
 *
 * CONCURRENCY
 *
 * The interface is neither MT nor ASYNC safe. In case of a multi-threaded user,
 * explicit serialisation is needed.
 *
 * TRANSPORTABILITY
 *
 * The code should compile on any reasonable UNIX. Linux-specific and
 * Darwin-specific bits (mostly setting the process name for escrowd) are
 * compiled conditionally.
 *
 */

#include <stdint.h>

/*
 * An escrow, which file descriptors can be sent to and retrieved from.
 *
 * This is an incomplete type.
 */
struct escrow;

enum {
        /* Start an escrowd if nobody is listening or the socket does not exists. */
        ESCROW_CREAT   = 1 << 0,
        /* Output errors and messages exchanged with escrowd on stderr. */
        ESCROW_VERBOSE = 1 << 1,
        /* Force unlink of the socket when a new escrowd is started. */
        ESCROW_FORCE   = 1 << 2
};

/*
 * Establishes a connection to escrowd, starting it if necessary.
 *
 * NR_TAGS is the number of tags that a newly started escrowd will have.
 */
int  escrow_init(const char *path, uint32_t flags, int32_t nr_tags, struct escrow **escrow);
/* Finalises the escrow connection. */
void escrow_fini(struct escrow *escrow);

/*
 * Returns information about a tag.
 *
 * In NR the maximal used index in this tag plus one is placed. Note, that some
 * indices less than the maximal one can be absent.
 *
 * In NOB the sum of payload sizes of all descriptors in the tag is placed. This
 * can be used to pre-allocate memory for payloads before the recovery.
 */
int escrow_tag(struct escrow *escrow, int16_t tag, int32_t *nr, int32_t *nob);
/*
 * Retrieves the descriptor with the given index in the given tag.
 *
 * *NOB contains the size of the payload buffer DATA.
 *
 * The retrieved descriptor is placed in *FD, the actual size of the payload is
 * returned in *NOB. The payload is copied into DATA, truncated at the original
 * size in *NOB if necessary.
 *
 * It is up to the user to close the returned file descriptor.
 */
int escrow_get(struct escrow *escrow, int16_t tag, int32_t idx, int *fd, int32_t *nob, void *data);
/* Places the descriptor and its payload in the escrow. */
int escrow_add(struct escrow *escrow, int16_t tag, int32_t idx, int  fd, int32_t  nob, void *data);
/* Deletes the descriptor and its payload from the escrow. */
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
