#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

typedef struct growbuffer {
    // The total number of bytes, and the start byte
    int size;
    // The start byte
    int start;
    // The blocks
    char data[ 64 * 1024 ];
    struct growbuffer *prev, *next;
} growbuffer;

// returns nonzero on success, zero on out of memory
extern int growbuffer_append(growbuffer **gb, const char *data, int dataLen);

extern void growbuffer_read(growbuffer **gb, int amt, int *amtReturn, char *buffer);

extern void growbuffer_destroy(growbuffer *gb);
