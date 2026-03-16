
#include <cstdint>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <assert.h>

int32_t read_full(int fd, char *buf, size_t n);

int32_t write_full(int fd, char *buf, size_t n);