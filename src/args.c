#include "args.h"
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parse_unsigned(const char *text, uint64_t maximum, uint64_t *result)
{
    if (text == NULL || result == NULL || *text == 0) return -1;
    uint64_t number = 0;
    for (const char *p = text; *p != 0; ++p) {
        if (*p < '0' || *p > '9') return -1;
        uint64_t digit = (uint64_t) (*p - '0');
        if (digit > maximum || number > (maximum - digit) / 10) return -1;
        number = number * 10 + digit;
    }
    *result = number;
    return 0;
}

int parse_position(const char *text, int64_t *result)
{
    if (text == NULL || result == NULL || *text == 0) return -1;
    const char *digits = *text == '-' ? text + 1 : text;
    if (*digits == 0) return -1;
    for (const char *p = digits; *p != 0; ++p) if (*p < '0' || *p > '9') return -1;
    errno = 0;
    char *end;
    long long number = strtoll(text, &end, 10);
    if (errno != 0 || *end != 0 || number < INT64_MIN || number > INT64_MAX) return -1;
    *result = (int64_t) number;
    return 0;
}

void args_usage(const char *program)
{
    fprintf(stderr,
        "Usage: %s COMMAND [-c CONFIG] [-n N] [-r R] [-w W] [-t TIMEOUT_MS] -- ARGS\n"
        "  put KEY VALUE      get KEY           cat KEY... DEST\n"
        "  substr KEY POS LEN DEST              find HAYSTACK_KEY NEEDLE_KEY\n"
        "  ring               help\n", program);
}

int args_parse(int argc, char **argv, options_t *options)
{
    if (argc < 2 || options == NULL) return -1;
    *options = (options_t) {.command = argv[1], .config = "servers.txt",
        .reads = 1, .writes = 1, .timeout_ms = DEFAULT_TIMEOUT_MS};
    int i = 2;
    while (i < argc && strcmp(argv[i], "--") != 0) {
        if (i + 1 >= argc) return -1;
        const char *flag = argv[i++], *value = argv[i++];
        uint64_t number;
        if (strcmp(flag, "-c") == 0) { options->config = value; continue; }
        uint64_t maximum = strcmp(flag, "-t") == 0 ? 60000 : MAX_NODES;
        if (parse_unsigned(value, maximum, &number) != 0 || number == 0) return -1;
        if (strcmp(flag, "-n") == 0) options->replicas = (size_t) number;
        else if (strcmp(flag, "-r") == 0) options->reads = (size_t) number;
        else if (strcmp(flag, "-w") == 0) options->writes = (size_t) number;
        else if (strcmp(flag, "-t") == 0) options->timeout_ms = (int) number;
        else return -1;
    }
    if (i < argc) ++i;
    options->operand_count = argc - i;
    options->operands = argv + i;
    return 0;
}
