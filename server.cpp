#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <assert.h>
#include "utilities.h"
#include <poll.h>

/*
Application protocol:
    byte stream is of form len1(4 bytes), msg1, len2(4 bytes), msg2, ...
    so when we read, we first read the 4 byte integer len, then read the corresponding payload
*/

/*
Concurrency:
    thread-based concurrency would be easy to implement, however, we want to scale to a large number of connections
    each thread has its own stack so there is significant overhead. Also with a growing number of connections, the memory usage per thread
    becomes increasingly difficult to control.

    Therefore, the concurrency our Redis cache uses will be event-based concurrency.

*/

/*
API basics:

    Each socket has a two independent kernel-side buffers for receiving and sending
    read() copies data from the receiving kernel-side buffer
    write() copies data to the sending kernel-side buffer

    note that write() does not engage with the network, it merely places data into a buffer.
    data is pulled from this send buffer and pushed onto the TCP stack, which sends the data over the network

    Chain for sending data from server:
    write to send buffer ->
    kernel send buffer pushes data onto serverTCP stack ->
    data sent to client TCP stack ->
    TCP reassembles and reorders and copies data into client receive stack ->
    read from buffer

    Blocking vs non-blocking:
        if receive buffer is empty, a blocking read will block and wait for more data while a non-blocking read will return with errno == EAGAIN
        if receive buffer is non-empty, both blocking and non-blocking reads return the data immediately

        if send buffer is full, a blockig write will block and wait until space becomes available in the buffer
        if send buffer is not full, both blocking and non-blocking writes will write to fill the buffer and return immediately

        we can set flags for blocking and non-blocking using the fcntl syscall.

    Readiness API:
        int poll(struct pollfd *fds, nfds_t nfds, int timeout)
        takes an array of struct pollfd of length nfds and blocks until at least one of them is ready.
        It edits the revents field of each fd to indicate the events that are ready
        returns number of sockets that are ready

        struct pollfd {
            int fd;
            short events; // request: read/write/both
            short revents; // ready: can read? can write?
        }
*/

/*
Pipelined requests (optimization)
    network latency makes sending requests from client to server expensive
    For each round trip (request -> response), a network latency induced cost is paid
    So for sequential processing (1 request and 1 response at time), a network latency is paid each round trip
    so 1000 network latencies are paid for 1000 roundtrips

    We can optimize this by using batch processing. Send all 1000 requests in the same batch, then send all 1000 replies in the same batch.
    This reduces the roundtrips from 1000 to 1, effectively reducing run time by a factor of 1000.
    Since we are only paying for the network latency of 1 roundtrip instead of 1000 and the network latency cost trumps the processing cost.
*/

static bool try_one_request(Conn *);
static void handle_read(Conn *);
static void handle_write(Conn *);


static bool try_one_request(Conn *conn) {
    // 3. try to parse
    if (conn->incoming.size() < 4) {
        return false;
    }
    uint32_t len {0};
    memcpy(&len, conn->incoming.data(), 4);
    if (len > k_max_msg) {
        conn->want_close = true;
        return false;
    }

    if (4 + len > conn->incoming.size()) {
        return false; // want read
    }

    const uint8_t *request = &conn->incoming[4];
    // 4. process
    // do_something
    printf("client says: len:%d data:%.*s\n",
        len, len < 100 ? len : 100, request);

    // generate reply (reply which an echo)
    buf_append(conn->outgoing, (const uint8_t *)&len, 4);
    buf_append(conn->outgoing, (const uint8_t *)request, (size_t)len);
    

    // 5. remove message
    buf_consume(conn->incoming, 4 + len);
    return true;
}

static Conn *handle_accept(int fd) {
    // accept
    struct sockaddr_in client_addr{};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr * )&client_addr, &addrlen);
    if (connfd < 0) {
        return NULL;
    }

    // set new connection to non-blocking
    fd_set_nonblock(connfd);

    // Create a new Conn struct
    Conn *conn = new Conn();
    conn->fd = connfd;
    conn->want_read = true; // read the first request
    return conn;
}

static void handle_read(Conn *conn) {
    // 1. Do a non-blocking read
    uint8_t buf[64* 1024];
    ssize_t rv = read(conn->fd, buf, sizeof(buf));
    if (rv <= 0) {
        conn->want_close = true;
        return;
    }
    // 2. Add new data to the Conn::incoming buffer
    buf_append(conn->incoming, (const uint8_t *)buf, (size_t)rv);
    // 3. Try to parse the accumulated buffer
    // 4. Process the parsed message
    // 5. Remove the message from "Conn::incoming"
    while (try_one_request(conn)) {}
    // update readiness intention
    // read complete if request successfully processed (buffer cleared), or no incoming requests
    if (conn->incoming.size() == 0) {
        conn->want_read = false;
        conn->want_write = true;
        handle_write(conn);
    }
}

static void handle_write(Conn *conn) {
    assert(conn->outgoing.size() > 0);
    ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());

    if (rv < 0 && errno == EAGAIN) {
        return; // not actually ready
    }

    if (rv < 0) {
        conn->want_close = true;
        return;
    }

    // remove written data from outgoing
    buf_consume(conn->outgoing, (size_t)rv);

    // update intentions
    // write complete if all data copied to send buffer (outgoing buffer cleared)
    if (conn->outgoing.size() == 0) {
        conn->want_read = true;
        conn->want_write = false;
    }
 }

// dummy processing
// one read and one write
[[maybe_unused]] static void do_something(int connfd) {
    char rbuf[64] {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        msg("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}

[[maybe_unused]] static int32_t one_request(int connfd) {
    // read 4 byte length header
    char rbuf[4 + k_max_msg];
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        // errno is not set to 0 if EOF
        // we set errno to 0 at start to distinguish EOF case from actual read() errors
        msg(errno == 0 ? "EOF" : "read() error");
        return err;
    }
    uint32_t len {0};
    memcpy(&len, rbuf, 4); // copy first 4 bytes of rbuf into len
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }
    // request body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() err");
        return err;
    }
    // do something
    printf("client says: %.*s\n", len, &rbuf[4]);
    // reply
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(wbuf + 4, reply, len);
    return write_full(connfd, wbuf, 4 + len);
}

int main() {
    // socket syscall returns a socket handle
    // AF_INET is for IPv4
    // SOCK_STREAM is for TCP. SOCK_DGRAM for UDP
    // third argument irrelevant for our uses
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!fd) { die("socket()"); }

    // we need to enable SO_RESUSEADDR for all listening sockets
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // struct sockaddr_in represents an IPv4:port pair
    // stored as big-endian numbers (most significant byte comes first)
    // htons() and htonl() are used to convert numbers into big-ending numbers
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(0);
    int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) { die("bind()"); }

    // Previous stages are passing parameters
    // The listening socket is created after listen is called
    // after listen(), the OS will automaticalle handle TCP connection attempts
    // and place established connections in queue from which applications can retrieve tham via accept().
    // listen
    rv = listen(fd, SOMAXCONN);
    if (rv) { die("listen()"); }

    // Before entering event loop, set listening socket to non-blocking
    fd_set_nonblock(fd);

    // Prepare the list of arguments for poll
    // fd2conn is a map backed by a vector
    std::vector<Conn *> fd2conn;
    std::vector<struct pollfd> poll_args;

    // event loop
    while (true) {
        // prepare arguments of the poll() call
        poll_args.clear();
        // put listening socket in first position
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        //rest are connection sockets
        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            // set pollfd flags based on intent
            if (conn->want_read) {
                pfd.events |= POLLIN;
            }
            if (conn->want_write) {
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        // call poll()
        // poll blocks (waits) until at least one socket is ready, returns number of ready sockets
        // poll is the only blocking syscall in the loop, and is the reason we can do concurrency without threads
        // the purpose of threads was to block and run another thread until its socket is ready
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if (rv < 0 && errno == EINTR) {
            // poll might occasionally return premptively due to kernel interrupts
            continue; // not an error
        }
        if (rv < 0) {
            die("poll");
        }

        // handle the listening socket if it is ready to accept a new connection
        // handle_accept returns a Conn object for the new connection
        if (poll_args[0].revents) {
            if (Conn *conn = handle_accept(fd)) {
                if (fd2conn.size() <= (size_t)conn->fd) {
                    fd2conn.resize(conn->fd + 1);
                }
                fd2conn[conn->fd] = conn;
            }
        }

        // handle connection sockets
        for (size_t i = 1; i < poll_args.size(); i++) { // note: skip the listening socket at 0

            uint32_t ready = poll_args[i].revents;
            Conn *conn = fd2conn[poll_args[i].fd];

            // handle read/writes if connection socket ready
            if (ready & POLLIN) {
                handle_read(conn);
            }
            if (ready & POLLOUT) {
                handle_write(conn);
            }

            // close connections due to socket error or request by application
            if ((ready & POLLERR) || conn->want_close) {
                (void)close(conn->fd);
                fd2conn[conn->fd] = NULL;
                delete conn;
            }
        }    
    }
    /*
    // After the listening socket is established, the server should enter a loop
    // that accepts and processes each client connection
    while (true) {
        // accept
        struct sockaddr_in client_addr {};
        socklen_t addrlen {sizeof(client_addr)};
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            continue; // error
        }
        
        // serves one client connection at once (no concurrency for now)
        // processes mutliple requests from same connection
        while (true) {
            // client sends a byte stream consisting of mutliple messages
            // we need to read 1 message and write 1 response for each message in the stream
            // so we need a high-level structure (application protocol) to split byte stream into messages
            // and a deserialization of the message (bytes to message)
            // byte stream protocol is 4 byte len followed by message
            int32_t err = one_request(connfd);
            if (err) {
                break;
            }
        }

        close(connfd);
    }
    return 0;
    */
}