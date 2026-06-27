#ifndef WAVE_RUNTIME_H
#define WAVE_RUNTIME_H

#include <stddef.h>

char *path_to_uri(const char *path);
int find_on_path(const char *name, char *out, size_t cap);
int wave_rg_binary(char *out, size_t cap);
const char *lsp_language_id(const char *name);
const char *lsp_server_argv(const char *name, const char *argv[5],
                            char *rt, size_t rtcap, char *cli, size_t clicap);

#endif
