#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <iostream>

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

// dummy processing
// one read and one write
static void do_something(int connfd) {
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
        do_something(connfd);
        close(connfd);
    }
    return 0;
}