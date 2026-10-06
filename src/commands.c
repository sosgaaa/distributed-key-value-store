#include "commands.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_command(const options_t *options, const char *name)
{
    return strcmp(options->command, name) == 0;
}

int commands_validate(const options_t *options)
{
    int n = options->operand_count;
    if (is_command(options, "help") || is_command(options, "ring")) return n == 0 ? 0 : -1;
    if ((is_command(options, "get") && n != 1) ||
        (is_command(options, "put") && n != 2) ||
        (is_command(options, "cat") && n < 2) ||
        (is_command(options, "find") && n != 2) ||
        (is_command(options, "substr") && n != 4)) return -1;
    if (!is_command(options, "get") && !is_command(options, "put") &&
        !is_command(options, "cat") && !is_command(options, "find") &&
        !is_command(options, "substr")) return -1;
    for (int i = 0; i < n; ++i) {
        if (is_command(options, "substr") && (i == 1 || i == 2)) continue;
        const char *text = options->operands[i];
        if (strlen(text) > MAX_ELEMENT || (*text == 0 && !(is_command(options, "put") && i == 1)))
            return -1;
    }
    if (is_command(options, "substr")) {
        int64_t position;
        uint64_t length;
        if (parse_position(options->operands[1], &position) != 0 ||
            parse_unsigned(options->operands[2], MAX_ELEMENT, &length) != 0) return -1;
    }
    return 0;
}

static int concatenate(const client_t *client, int argc, char **argv)
{
    char *result = malloc(MAX_ELEMENT + 1);
    if (result == NULL) return -1;
    size_t used = 0;
    for (int i = 0; i < argc - 1; ++i) {
        char *value = NULL;
        if (network_get(client, argv[i], &value) != 0) { free(result); return -1; }
        size_t length = strlen(value);
        if (length > MAX_ELEMENT - used) { free(value); free(result); return -1; }
        memcpy(result + used, value, length);
        used += length;
        free(value);
    }
    result[used] = 0;
    int status = network_put(client, argv[argc - 1], result);
    free(result);
    return status;
}

static int substring(const client_t *client, char **argv)
{
    int64_t position;
    uint64_t length;
    if (parse_position(argv[1], &position) != 0 ||
        parse_unsigned(argv[2], MAX_ELEMENT, &length) != 0) return -1;
    char *value = NULL;
    if (network_get(client, argv[0], &value) != 0) return -1;
    size_t size = strlen(value);
    /* Check bounds before addition, including INT64_MIN and huge offsets. */
    if (position < -(int64_t) size || position > (int64_t) size) { free(value); return -1; }
    size_t start = position < 0 ? (size_t) ((int64_t) size + position) : (size_t) position;
    if (length > size - start) { free(value); return -1; }
    value[start + (size_t) length] = 0;
    int status = network_put(client, argv[3], value + start);
    free(value);
    return status;
}

int commands_run(const client_t *client, const options_t *options)
{
    char **argv = options->operands;
    int result;
    if (is_command(options, "get")) {
        char *value = NULL;
        result = network_get(client, argv[0], &value);
        if (result == 0) printf("OK %s\n", value);
        else puts("FAIL");
        free(value);
    } else if (is_command(options, "find")) {
        char *haystack = NULL, *needle = NULL;
        result = network_get(client, argv[0], &haystack);
        if (result == 0) result = network_get(client, argv[1], &needle);
        if (result == 0) {
            const char *match = strstr(haystack, needle);
            printf("OK %td\n", match == NULL ? (ptrdiff_t) -1 : match - haystack);
        } else puts("FAIL");
        free(haystack);
        free(needle);
    } else {
        if (is_command(options, "put")) result = network_put(client, argv[0], argv[1]);
        else if (is_command(options, "cat")) result = concatenate(client, options->operand_count, argv);
        else result = substring(client, argv);
        puts(result == 0 ? "OK" : "FAIL");
    }
    return result == 0 ? 0 : 1;
}
