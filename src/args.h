#ifndef DKVS_ARGS_H
#define DKVS_ARGS_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const char *command;
    const char *config;
    size_t replicas, reads, writes;
    int timeout_ms, operand_count;
    char **operands;
} options_t;
int parse_unsigned(const char *text, uint64_t maximum, uint64_t *result);
int parse_position(const char *text, int64_t *result);
int args_parse(int argc, char **argv, options_t *options);
void args_usage(const char *program);
#endif
