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


static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
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

static int32_t one_request(int connfd) {
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
}