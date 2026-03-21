
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "utilities.h"
#include <sstream>

static int32_t parse_command(std::string text, char *wbuf) {
    // tokenise
    std::vector<std::string> tokens{};
    std::stringstream input{text};
    std::string intermediate{};

    while (getline(input, intermediate, ' ')) {
        if (!intermediate.empty()) {
            tokens.push_back(intermediate);
        }
    }

    uint32_t nstr = tokens.size();
    uint32_t len{4};
    char *cur = wbuf + 4;

    memcpy(cur, &nstr, 4);
    cur += 4;

    for (std::string token : tokens) {
        size_t cur_len = strlen(token.data());
        len += 4 + cur_len;
        if (len > k_max_msg) {
            return -1;
        }
    

        memcpy(cur, &cur_len, 4);
        memcpy(cur + 4, token.data(), cur_len);
        cur += 4 + cur_len;
    }

    memcpy(wbuf, &len, 4);
    return 0;
}


static int32_t send_req(int fd, std::string text) {
    char wbuf[4 + k_max_msg];
    // msg is of form nstr len str1 len str2 ... len strn
    if (int32_t err = parse_command(text, wbuf)) {
        return err;
    }
    // read the payload length that parse_command wrote into wbuf
    uint32_t len {};
    memcpy(&len, wbuf, 4);
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

    for (std::string s : data) {
        int32_t err = send_req(fd, s);
        if (err) {
            return err;
        }
        err = read_res(fd, rbuf);
        if (err) {
            return err;
        }
        // response format: [4B resp_len] [4B status] [data]
        uint32_t resp_len {};
        memcpy(&resp_len, rbuf, 4);
        uint32_t status {};
        memcpy(&status, rbuf + 4, 4);
        uint32_t data_len = resp_len - 4;
        printf("server says: [status=%u] %.*s\n", status, data_len, &rbuf[8]);
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
        {"set 1 hello", "set 2 world", "get 1", "get 2", "not a command"
        }
    };

    query(fd, queries);

    close(fd);
    return 0;
}