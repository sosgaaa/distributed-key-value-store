#include "args.h"
#include "commands.h"
#include "network.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    options_t options;
    if (args_parse(argc, argv, &options) != 0 || commands_validate(&options) != 0) {
        args_usage(argv[0]);
        return 2;
    }
    if (strcmp(options.command, "help") == 0) {
        args_usage(argv[0]);
        return 0;
    }
    ring_t ring;
    if (ring_load(&ring, options.config) != 0) {
        fprintf(stderr, "Could not load ring configuration: %s\n", options.config);
        return 2;
    }
    if (strcmp(options.command, "ring") == 0) {
        ring_print(&ring);
        ring_destroy(&ring);
        return 0;
    }
    if (options.replicas == 0) options.replicas = ring.server_count;
    if (options.replicas > ring.server_count || options.reads > options.replicas ||
        options.writes > options.replicas) {
        fprintf(stderr, "Require 1 <= R,W <= N <= physical server count\n");
        ring_destroy(&ring);
        return 2;
    }
    client_t client = {
        .ring = &ring, .replicas = options.replicas, .read_quorum = options.reads,
        .write_quorum = options.writes, .timeout_ms = options.timeout_ms
    };
    int result = commands_run(&client, &options);
    ring_destroy(&ring);
    return result;
}
