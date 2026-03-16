
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "utilities.h"

static void die(const char *msg) {
    int err = errno; // errno is a global variable conventionally set when errors are encountered
    fprintf(stderr, "[%d], %s\n", err, msg);
    abort();
}

// send 
static int32_t query(int fd, const char *text) {
    uint32_t len = (uint32_t)strlen(text);

    if (len > k_max_msg) {
        return -1;
    }

    // send
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(wbuf + 4, text, len);
    if (int32_t err = write_full(fd, wbuf, 4 + len)) {
        return err;
    }

    // read reply
    char rbuf[4 + k_max_msg];
    errno = 0;
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return -1;
    }
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // do_something
    printf("server says: %.*s\n", len, &rbuf[4]);
    return 0;
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    // connect
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
    if (rv) {
        die("connect");
    }

    // do_something
    int32_t err = query(fd, "hello1");
    if (err) {
        goto L_DONE;
    }
    err = query(fd, "hello2");
    if (err) {
        goto L_DONE;
    }

    L_DONE:
    // close
        close(fd);
        return 0;
}