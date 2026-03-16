
#include <cstdint>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <assert.h>

const size_t k_max_msg = 4096;

void msg(const char *msg);

int32_t read_full(int fd, char *buf, size_t n);

int32_t write_full(int fd, const char *buf, size_t n);