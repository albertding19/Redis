
#include <cstdint>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <assert.h>
#include <fcntl.h>
#include <vector>
#include "conn.h"

void die(const char *msg);

void msg(const char *msg);

void fd_set_nonblock(int fd);

void fd_set_block(int fd);

int32_t read_full(int fd, char *buf, size_t n);

int32_t write_full(int fd, const char *buf, size_t n);