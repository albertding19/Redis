
#include <cstdint>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <assert.h>
#include <fcntl.h>
#include <vector>

const size_t k_max_msg = 4096;

void die(const char *msg);

void msg(const char *msg);

void buf_append(std::vector<uint8_t> &, const uint8_t *, size_t);

void buf_consume(std::vector<uint8_t> &, size_t);

void fd_set_nonblock(int fd);

void fd_set_block(int fd);

int32_t read_full(int fd, char *buf, size_t n);

int32_t write_full(int fd, const char *buf, size_t n);

// state for each connection
/*
    Conn::want_read and Conn::want_write represents the intention of this connection
    Conn::want_close tells the event loop when to destroy the connection
    Conn::incoming buffers data from the socket's read buffer for the application protocol to parse
    Conn::outgoing buffers data that will be written to the socket's write buffer

    In event-based concurrency, data will be copied from the guaranteed non-empty receive buffer (by readiness API)
    to Conn::incoming. Conn::incoming then tries to parse the data. If there is not enough data (i.e. some data is still transporting),
    then do nothing. Otherwise, if the message is succesfully parsed, then we call do_something on it then remove the message from Conn::incoming.
    This essentially simulates read_full, as we are reading continually until the data can be parsed by the application protocol.
    For a large stream of data, transporting takes non-trivial time, so mutiple loop iterations of read may be required to read the full message.

    Similarly for writes. A large response may take multiple loop iterations to completey pass into the send buffer. 
    So the response data must be stored in Conn::outgoing.
*/
struct Conn {
    int fd;
    // intention
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    // buffered inputs and outputs
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> outgoing;
};