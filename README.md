File descriptor escrow library
=============================

by Nikita Danilov <danilov@gmail.com>

BUILD
-----

    ./build

This produces:

  - escrow.o: the library object file that you can link into your binary or library
  - escrowd: the daemon binary that can be started in advance
  - echo-server, echo-client: a sample client and server demonstrating the use of the escrow library.

OVERVIEW
--------

This library provides an interface to send process' file descriptors to a
separate process ("escrow daemon", "escrowd"). The descriptors can be
retrieved later by the sender process or by another process.

The motivating use case for the library is zero-downtime service upgrade:
consider a network service that maintains socket connections from multiple
clients. To upgrade the service to a new version:

  - bring it to a "quiescent state" that is, pause accepting new client
    connections and new requests from the existing connections, and
    complete all ongoing requests;
  - place all the sockets in the escrow;
  - exit the service process;
  - start the new version;
  - retrieve all sockets from the escrow;
  - resume request and connection processing.

From the client perspective this process is transparent (save for a delay): the
connection to the server is not broken. Note that escrowd is single-threaded and
can have at most a single client at a time. Hence, the new service version
binary can start before the previous instance terminated: it will be safely
blocked in an attempt to connect to escrowd until the previous instance
disconnects.

The same mechanism can be used for recovery after a process crash, except in
this case there is no guarantee that the connections were left in some known
state, and the recovery code needs to figure out how to proceed.

An escrow can also be used to provide access to "restricted" file
descriptors: a priviledged process can open a device or establish and
authenticate a connection and then place the resulting file descriptor in an
escrow, from which it can be retrieved by any properly authorised process.

INTERFACE OVERVIEW
------------------

An escrow connection is established by calling `escrow_init()`. A parameter of
this function is the path to a UNIX domain socket used for the communication
with the escrowd. Access to the escrow is authorised by the usual access
rules for this pathname.

An escrowd process listening on the socket can be started explictly in
advance. Alternatively, when `escrow_init()`, called with `ESCROW_CREAT` flag,
determines that nobody is listening on the socket or the socket does not
exist, it starts the daemon automatically.

When a file descriptor is placed in an escrow, the user specifies a 16-bit
tag and a 32-bit index within the tag. Tags can be used to simplify descriptor
recovery. For example, in the service upgrade scenario described above, the
service can place all listener sockets in one tag and all accepter stream
sockets in another. The recovery can first recover all listeners and then all
streams. The total number of tags is specified when the escrow is created.

In addition to the tag and the index, a file descriptor has an optional
"payload" of up to 32KB. The payload is stored in and retrieved from the escrow
together with the file descriptor. In fact, it is possible to store and retrieve
payload only without a file descriptor, by providing (-1) as `fd` argument to
`escrow_add()`. This makes escrowd a simple memory-only data-base. A typical use
of payload is to store auxiliary information about the file descriptor that
recovery uses to restore application-specific per-descriptor state.

RETURN VALUES
-------------

All functions return 0 on success, a negated errno value on a failure.

CONCURRENCY
-----------

The interface is neither MT nor ASYNC safe. In case of a multi-threaded user,
explicit serialisation is needed.

EXAMPLES
--------

`echo-client.c` and `echo-server.c` are a simple echo client and service: the
single-threaded server listens on the local port `8087`, accepts connections
there and echoes back everything received form a client. The implementations are
deliberately simplified, they are not supposed to represent how socket services
should be implemented.

`echo-server.c` demonstrates the use of an escrow. The simplified echo-server
code is the following:

```
int main() {
        int            sock;
        struct escrow *escrow;
        int            result = escrow_init(argv[1], ESCROW_VERBOSE | ESCROW_FORCE, 1, &escrow);
        /*
         * Retrive the socket from the escrow.
         *
         * If this is the first time the server connects to the escrow, this
         * returns -ENOENT and places -1 into sock.
         *
         * If the echo-server is restarted, the escrow returns the socket that
         * the previous instance of the echo-server placed there.
         */
        escrow_get(escrow, 0, 0, &sock, &nob, &ch);
        if (sock == -1) {
                /* Create sock, bind, listen and accept ... */
                /* Place the accepted stream socket in the escrow. */
                escrow_add(escrow, 0, 0, sock, nob, &ch);
        }
        /* Loop until the client disconnects, echo back. */
        while ((result = read(sock, &ch, sizeof ch)) > 0) {
                write(sock, &ch, result);
        }
        /* Delete from the escrow. */
        escrow_del(escrow, 0, 0);
        escrow_fini(escrow);
        return 0;
```
Execute the following commands:
```
# Run the echo-server, using "usock" to communicate with escrowd.
# This will start an instance of escrowd.
./echo-server usock
```
In another shell, run the client:
```
# Start the client. It will connect to the server and loop forever,
# ... verifying that the server echoes back correctly.
./echo-client
```
Kill the server:
```
pkill -9 echo-server
```
Note that the client is still connected, because the socket descriptor is stored in escrow.
Start the server again.
It retrieves the socket from the escrow and continues echoing to the client.
```
./echo-server usock
```
You can check (via top) that client and server are busy comminicating.

Alternatively, you can start `escrowd` in advance as `escrowd ./usock`.

`echo-server` calls `escrow_init()` with `ESCROW_VERBOSE` flag, so you can see
the message exchange between the echo-server and escrowd:

```
Starting escrowd (111).   # escrow_init() starts new escrowd, because it got ECONNREFUSED on the socket
Listening on "usock"      # escrowd listens on the UNIX domain socket.
Connected to "usock"      # echo-server connects to the UNIX domain socket (again).
send: {GET   0   0} (-1)   0 # echo-server tries to retrieve the TCP socket from the escrow
recv: {GET   0   0} (-1)   0 # It's not there (yet).
send: {REP  -2 "Non-existent index in a GET request."} (-1)   0 # ENOENT reply is sent back
recv: {REP  -2 "Non-existent index in a GET request."} (-1)   0 # the reply is received by echo-server
Received from the escrowd: -2 "Non-existent index in a GET request."
send: {ADD   0   0   5    0} (5)   0 # At this point, the client connects and echo-server places the accepted stream socket in escrow
recv: {ADD   0   0   5    0} (6)   0 # escrowd receives the socket
send: {REP   0 ""} (-1)   0          # Reply is sent ...
recv: {REP   0 ""} (-1)   0          # ... and received.
Killed                               # echo-server is killed.
Connected to "usock"                 # echo-server restarts and connects to "usock".
send: {GET   0   0} (-1)   0         # echo-server tries to retrieve the TCP socket from the escrow.
recv: {GET   0   0} (-1)   0         # escrowd receives the request.
send: {ADD   0   0   5    0} (6)   0 # escrowd replies with the socket descriptor that ... 
recv: {ADD   0   0   5    0} (4)   0 # ... the previous instance of echo-server placed.
```
