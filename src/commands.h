#ifndef DKVS_COMMANDS_H
#define DKVS_COMMANDS_H
#include "args.h"
#include "network.h"
/* CLI exit codes: 0 success; 1 failed operation; 2 invalid arguments. */
int commands_validate(const options_t *options);
int commands_run(const client_t *client, const options_t *options);
#endif
