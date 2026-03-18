
#include "buffer.h"

struct Buffer *buffer_init(size_t len){
    struct Buffer *buffer = new(struct Buffer);
    uint8_t *ptr {(uint8_t *)malloc(len)};
    buffer->buffer_begin = ptr;
    buffer->data_begin = ptr;
    buffer->data_end = ptr;
    buffer->buffer_end = ptr + len;
    buffer->reallocs = 0;

    return buffer;
}

size_t buf_size(struct Buffer *buf) {
    return buf->data_end - buf->data_begin;
}

// append to the back
void buf_append(struct Buffer *buf, const uint8_t *data, size_t len) {
    size_t remaining_space = (buf->data_begin - buf->buffer_begin) + (buf->buffer_end - buf->data_end);
    if (len > remaining_space) { //realloc

        // firstly, shift as buffer length must be less that or equal to 4096
        size_t data_len = buf->data_end - buf->data_begin;
        memcpy(buf->buffer_begin, buf->data_begin, data_len);
        buf->data_begin = buf->buffer_begin;
        buf->data_end = buf->data_begin + data_len;

        // reallocation
        while (buf_size(buf) <= k_max_msg && len > remaining_space) {
            buf->reallocs += 1;
            size_t offset1 {static_cast<size_t>(buf->data_begin - buf->buffer_begin)};
            size_t offset2 {static_cast<size_t>(buf->data_end - buf->data_begin)};
            size_t newLength = growth_factor * buf->reallocs * (buf->buffer_end - buf->buffer_begin);
            uint8_t *ptr {(uint8_t *)realloc(buf->buffer_begin, newLength)};

            buf->buffer_begin = ptr;
            buf->buffer_end = ptr + newLength;
            buf->data_begin = ptr + offset1;
            buf->data_end = ptr + offset2;

            remaining_space = (buf->data_begin - buf->buffer_begin) + (buf->buffer_end - buf->data_end);
        }

        // append
        memcpy(buf->data_end, data, len);
        buf->data_end += len;
    }
    else if (len > static_cast<size_t>(buf->buffer_end - buf->data_end)) { // not enough space at back, shift
        // shift data to start of buffer
        size_t offset = buf->data_end - buf->data_begin;
        memcpy(buf->buffer_begin, buf->data_begin, offset);
        buf->data_begin = buf->buffer_begin;
        buf->data_end = buf->data_begin + offset;

        // append
        memcpy(buf->data_end, data, len);
        buf->data_end += len;
    }
    else { // enough space at back, simply append
        memcpy(buf->data_end, data, len);
        buf->data_end += len;
    }
}   

// remove from front
void buf_consume(struct Buffer *buf, size_t n) {
    buf->data_begin += n;
}

// free all memory allocated to buffer
void buf_free(struct Buffer *buf) {
    // free the contiguous malloc
    free(buf->buffer_begin);
    delete(buf);
}