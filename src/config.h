#ifndef DKVS_CONFIG_H
#define DKVS_CONFIG_H

/* Maximum IPv4 UDP payload: 65507 = 2 * 32753 + 1. */
enum {
    MAX_ELEMENT = 32753,
    MAX_MESSAGE = 2 * MAX_ELEMENT + 1,
    MAX_NODES = 1024,
    DEFAULT_TIMEOUT_MS = 300,
    DUMP_CHUNK_SIZE = 60000
};

#ifdef DEBUG
#include <stdio.h>
#define debug_log(...) fprintf(stderr, __VA_ARGS__)
#else
#define debug_log(...) ((void) 0)
#endif

#endif
