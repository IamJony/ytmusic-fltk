#ifndef SANSON_STREAM_H
#define SANSON_STREAM_H
#ifdef __cplusplus
extern "C" {
#endif
#define SP_SKIP_ADS 1
#define SP_LIVE 2
#define SP_AAC_TS 4
typedef struct {
    char *url;
    char *name;
    char *audio;
    int width, height, bandwidth;
    double fps;
} sp_variant;
int sp_start(void);
int sp_port(void);
char *sp_add_range(const char *url, long long length, const char *referer);
char *sp_add_direct(const char *url);
char *sp_add_hls(const char *url, int flags, double start);
void sp_clear(void);
int sp_hls_variants(const char *master_url, sp_variant **out);
int sp_hls_fmp4(const char *media_url);
int sp_in_ad(void);
void sp_free_variants(sp_variant *v, int n);
long long sp_url_clen(const char *url);
#ifdef __cplusplus
}
#endif
#endif
