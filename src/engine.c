#define _GNU_SOURCE
#include "engine.h"
#include "hash.h"
#include "ws.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sj_plugin { int dummy; };

char *sj_read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = malloc(n + 1);
    if (fread(s, 1, n, f) != (size_t)n) { fclose(f); free(s); return NULL; }
    fclose(f); s[n] = 0;
    if (len) *len = n;
    return s;
}

sj_plugin *sj_open(const char *plugin_dir, const char *host_js, const char *settings_json,
                   const char *auth_json, const char *save_state, sj_log_fn log, void *log_user,
                   char **error) {
    (void)plugin_dir; (void)host_js; (void)settings_json;
    (void)auth_json; (void)save_state; (void)log; (void)log_user;
    sj_plugin *p = calloc(1, sizeof *p);
    if (!p) {
        if (error) *error = strdup("sin memoria");
        return NULL;
    }
    return p;
}

char *sj_call(sj_plugin *p, const char *name, const char *args_json) {
    (void)p; (void)name; (void)args_json;
    return NULL;
}

char *sj_next(sj_plugin *p, int pager, const char *arg_json) {
    (void)p; (void)pager; (void)arg_json;
    return NULL;
}

char *sj_eval(sj_plugin *p, const char *code) {
    (void)p; (void)code;
    return NULL;
}

void sj_close(sj_plugin *p) { free(p); }

char *sj_sapisid_header(const char *cookies, const char *origin) {
    (void)cookies; (void)origin;
    return NULL;
}
