
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include "protocol.h"

const size_t buf_size_initial {128};
const int growth_factor {2};

struct Buffer {
    uint8_t *buffer_begin;
    uint8_t *buffer_end;
    uint8_t *data_begin;
    uint8_t *data_end;
    int reallocs;
};

struct Buffer *buffer_init(size_t);

size_t buf_size(struct Buffer *);

void buf_append(struct Buffer *, const uint8_t *, size_t);

void buf_consume(struct Buffer *, size_t);

void buf_free(struct Buffer *);
