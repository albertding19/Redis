
#include "utilities.h"

void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

// sets socket flags to non-blocking for reads and writes
void fd_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL");
    }
}

// sets socket flags to blocking for reads and writes
void fd_set_block(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return;
    }
    flags &= ~O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL");
    }
}

// reads a single message of byte size n
int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        // read returns the amount of bytes read
        // however, read can read less than the required number of bytes n
        // because read reads what's currently in the receive buffer fd, data may still be in transfer over the the network
        // so we do this in a loop, to ensure that all n bytes are read
        // read(fd, buf, n) reads n bytes from the socket's receive buffer into buf
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1; // error, or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;

        // advance the buf pointer by number of bytes read
        buf += rv;
    }
    return 0;
}

// write a single message of byte size n
int32_t write_full(int fd, const char *buf, size_t n) {
    while (n > 0) {
        // write copies up to n bytes from buf into the socket's send buffer
        // TCP will eventually deliver messages stored in the send buffer
        // like with read, there is no gurantee that n bytes are copied each time because the buffer may be full
        // so we do a loop again in order to ensure that n bytes are copied
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1; // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;

        // advance the buf pointer by number of bytes copied
        buf += rv;
    }
    return 0;
}