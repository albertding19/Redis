
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "utilities.h"

static int32_t send_req(int fd, std::string text, uint32_t &len) {
    len = (uint32_t)strlen(text.data());
    if (len > k_max_msg) {
        return -1;
    }
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(wbuf + 4, text.data(), len);
    if (int32_t err = write_full(fd, wbuf, 4 + len)) {
        return err;
    }
    return 0;
}

static int32_t read_res(int fd, char *buf) {
    errno = 0;
    int32_t err = read_full(fd, buf, 4);
    if (err) {
        msg(errno == 0 ? "EOF" : "read() error");
        return -1;
    }
    uint32_t len {};

    memcpy(&len, buf, 4);
    if (len > k_max_msg) {
        msg("too large");
        return -1;
    }

    err = read_full(fd, buf + 4, len);
    if (err) {
        msg("read() error");
        return err;
    }
    
    return 0;
}

static int32_t query(int fd, std::vector<std::string> data) {
    char rbuf[4 + k_max_msg];
    uint32_t len {};

    for (std::string s : data) {
        int32_t err = send_req(fd, s, len);
        if (err) {
            return err;
        }
        err = read_res(fd, rbuf);
        if (err) { 
            return err;
        }
        printf("server says: %.*s\n", len, &rbuf[4]);
    }

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

    std::vector<std::string> queries {
        {"hello", "hello1", "hello2", "hello3", 
            std::string(k_max_msg, 'z'), // very large query, requies multiple event loop iterations
            "hello5"
        }
    };

    query(fd, queries);

    close(fd);
    return 0;
}