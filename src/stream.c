/* Proxy local para streams de YouTube/YouTube Music. Copiado de Sanson. */
#define _GNU_SOURCE
#include "stream.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

#define SP_UA "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:128.0) Gecko/20100101 Firefox/128.0"
#define SP_CHUNK (1024 * 1024)
#define SP_MAX 64
enum { K_RANGE, K_HLS, K_DIRECT };
typedef struct {
    int id; int kind;
    char *url; char *referer;
    long long length; int flags; double start;
} sp_entry;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static sp_entry entries[SP_MAX];
static int nentries, next_id = 1, listen_fd = -1, port;

typedef struct { char *data; size_t len, cap; } sbuf;
static void sbuf_add(sbuf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 8192;
        while (cap < b->len + n + 1) cap *= 2;
        b->data = realloc(b->data, cap); b->cap = cap;
    }
    memcpy(b->data + b->len, s, n); b->len += n; b->data[b->len] = 0;
}
static size_t to_buf(char *p, size_t s, size_t n, void *ud) {
    sbuf_add(ud, p, s * n); return s * n;
}
typedef struct { int fd; int gone; long long sent; } sink;
static int send_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; n -= w;
    }
    return 0;
}
static size_t to_sock(char *p, size_t s, size_t n, void *ud) {
    sink *k = ud;
    if (send_all(k->fd, p, s * n)) { k->gone = 1; return 0; }
    k->sent += s * n; return s * n;
}
static CURL *new_curl(const char *url, struct curl_slist *hl) {
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, SP_UA);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    if (hl) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    return c;
}
static int fetch_text(const char *url, sbuf *out) {
    CURL *c = new_curl(url, NULL);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_buf);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    return rc == CURLE_OK && code >= 200 && code < 300 ? 0 : -1;
}
static char *resolve(const char *base, const char *uri) {
    char *r;
    if (strstr(uri, "://")) return strdup(uri);
    if (uri[0] == '/') {
        const char *h = strstr(base, "://");
        const char *e = h ? strchr(h + 3, '/') : NULL;
        int n = e ? (int)(e - base) : (int)strlen(base);
        if (asprintf(&r, "%.*s%s", n, base, uri) < 0) return NULL;
        return r;
    }
    const char *q = strchr(base, '?');
    int n = q ? (int)(q - base) : (int)strlen(base);
    while (n > 0 && base[n - 1] != '/') n--;
    if (asprintf(&r, "%.*s%s", n, base, uri) < 0) return NULL;
    return r;
}
static const char *attr(const char *line, const char *key, char *out, size_t sz) {
    size_t kl = strlen(key);
    const char *p = line;
    while ((p = strstr(p, key))) {
        if ((p == line || p[-1] == ',' || p[-1] == ':') && p[kl] == '=') {
            p += kl + 1;
            size_t i = 0;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && i + 1 < sz) out[i++] = *p++;
            } else {
                while (*p && *p != ',' && *p != '\r' && *p != '\n' && i + 1 < sz) out[i++] = *p++;
            }
            out[i] = 0; return out;
        }
        p += kl;
    }
    return NULL;
}
int sp_hls_variants(const char *master, sp_variant **out) {
    sbuf b = {0}; *out = NULL;
    if (fetch_text(master, &b) || !b.data) return -1;
    char *media_ids[32], *media_names[32], *media_audio[32];
    int nmedia = 0, n = 0, cap = 0;
    sp_variant *v = NULL;
    char *save, *line, *pending = NULL;
    for (line = strtok_r(b.data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        line[strcspn(line, "\r")] = 0;
        char a[4096];
        if (!strncmp(line, "#EXT-X-MEDIA:", 13) && nmedia < 32 && attr(line, "GROUP-ID", a, sizeof a)) {
            media_ids[nmedia] = strdup(a);
            media_names[nmedia] = strdup(attr(line, "NAME", a, sizeof a) ? a : "");
            media_audio[nmedia] = NULL;
            char t[16];
            if (attr(line, "TYPE", t, sizeof t) && !strcmp(t, "AUDIO") && attr(line, "URI", a, sizeof a))
                media_audio[nmedia] = resolve(master, a);
            nmedia++;
        } else if (!strncmp(line, "#EXT-X-STREAM-INF:", 18)) {
            free(pending); pending = strdup(line);
        } else if (pending && line[0] && line[0] != '#') {
            if (n == cap) { cap = cap ? cap * 2 : 8; v = realloc(v, cap * sizeof *v); }
            sp_variant *x = &v[n++];
            memset(x, 0, sizeof *x);
            x->url = resolve(master, line);
            if (attr(pending, "BANDWIDTH", a, sizeof a)) x->bandwidth = atoi(a);
            if (attr(pending, "RESOLUTION", a, sizeof a)) sscanf(a, "%dx%d", &x->width, &x->height);
            if (attr(pending, "FRAME-RATE", a, sizeof a)) x->fps = atof(a);
            if (attr(pending, "AUDIO", a, sizeof a))
                for (int i = 0; i < nmedia && !x->audio; i++)
                    if (!strcmp(media_ids[i], a) && media_audio[i]) x->audio = strdup(media_audio[i]);
            if (attr(pending, "VIDEO", a, sizeof a)) {
                for (int i = 0; i < nmedia; i++)
                    if (!strcmp(media_ids[i], a) && media_names[i][0]) x->name = strdup(media_names[i]);
                if (!x->name && strcmp(a, "chunked")) x->name = strdup(a);
            }
            if (!x->name) {
                char nm[32];
                if (x->height) snprintf(nm, sizeof nm, "%dp%s", x->height, x->fps > 31 ? "60" : "");
                else snprintf(nm, sizeof nm, "audio");
                x->name = strdup(nm);
            }
            free(pending); pending = NULL;
        }
    }
    free(pending);
    for (int i = 0; i < nmedia; i++) { free(media_ids[i]); free(media_names[i]); free(media_audio[i]); }
    free(b.data);
    if (!n) { v = calloc(1, sizeof *v); v->url = strdup(master); v->name = strdup("Auto"); n = 1; }
    *out = v; return n;
}
void sp_free_variants(sp_variant *v, int n) {
    for (int i = 0; i < n; i++) { free(v[i].url); free(v[i].name); free(v[i].audio); }
    free(v);
}
long long sp_url_clen(const char *url) {
    const char *p = strstr(url, "clen=");
    while (p && p[-1] != '?' && p[-1] != '&') p = strstr(p + 5, "clen=");
    return p ? atoll(p + 5) : 0;
}
static char *make_url(int id) {
    char *u;
    if (asprintf(&u, "http://127.0.0.1:%d/s/%d", port, id) < 0) return NULL;
    return u;
}
static char *add(int kind, const char *url, long long len, const char *referer, int flags, double start) {
    pthread_mutex_lock(&lock);
    if (nentries == SP_MAX) {
        free(entries[0].url); free(entries[0].referer);
        memmove(entries, entries + 1, (SP_MAX - 1) * sizeof *entries);
        nentries--;
    }
    sp_entry *e = &entries[nentries++];
    e->id = next_id++; e->kind = kind; e->url = strdup(url);
    e->referer = referer ? strdup(referer) : NULL;
    e->length = len; e->flags = flags; e->start = start;
    char *u = make_url(e->id);
    pthread_mutex_unlock(&lock);
    return u;
}
char *sp_add_range(const char *url, long long length, const char *referer) { return add(K_RANGE, url, length, referer, 0, 0); }
char *sp_add_direct(const char *url) { return add(K_DIRECT, url, 0, NULL, 0, 0); }
char *sp_add_hls(const char *url, int flags, double start) { return add(K_HLS, url, 0, NULL, flags, start); }
static volatile time_t ad_seen;
int sp_in_ad(void) { return ad_seen && time(NULL) - ad_seen < 8; }
int sp_hls_fmp4(const char *media_url) {
    sbuf b = {0};
    int r = !fetch_text(media_url, &b) && b.data && strstr(b.data, "#EXT-X-MAP:");
    free(b.data); return r;
}
void sp_clear(void) {
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nentries; i++) { free(entries[i].url); free(entries[i].referer); }
    nentries = 0; ad_seen = 0;
    pthread_mutex_unlock(&lock);
}
static int find(int id, sp_entry *copy) {
    int ok = 0;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nentries; i++)
        if (entries[i].id == id) {
            *copy = entries[i];
            copy->url = strdup(entries[i].url);
            copy->referer = entries[i].referer ? strdup(entries[i].referer) : NULL;
            ok = 1;
        }
    pthread_mutex_unlock(&lock);
    return ok;
}
static void reply(int fd, const char *status, const char *extra) {
    char h[512];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %s\r\nConnection: close\r\n%s\r\n", status, extra ? extra : "");
    send_all(fd, h, n);
}
static void serve_range(int fd, sp_entry *e, long long a, long long b) {
    long long total = e->length;
    if (total > 0 && b < 0) b = total - 1;
    if (total > 0 && a >= total) {
        char x[96]; snprintf(x, sizeof x, "Content-Range: bytes */%lld\r\n", total);
        reply(fd, "416 Range Not Satisfiable", x); return;
    }
    char hdr[512];
    if (total > 0)
        snprintf(hdr, sizeof hdr, "Content-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\n"
                 "Content-Length: %lld\r\nContent-Range: bytes %lld-%lld/%lld\r\n", b - a + 1, a, b, total);
    else
        snprintf(hdr, sizeof hdr, "Content-Type: application/octet-stream\r\n");
    reply(fd, total > 0 && a > 0 ? "206 Partial Content" : "200 OK", hdr);
    struct curl_slist *hl = NULL;
    if (e->referer) {
        char line[256];
        snprintf(line, sizeof line, "Origin: %s", e->referer); hl = curl_slist_append(hl, line);
        snprintf(line, sizeof line, "Referer: %s/", e->referer); hl = curl_slist_append(hl, line);
    }
    long long pos = a; int rn = 0, fails = 0;
    sink k = {fd, 0, 0};
    while (!k.gone && (b < 0 || pos <= b)) {
        long long end = pos + SP_CHUNK - 1;
        if (b >= 0 && end > b) end = b;
        char *u;
        if (asprintf(&u, "%s%srange=%lld-%lld&rn=%d", e->url, strchr(e->url, '?') ? "&" : "?", pos, end, rn++) < 0) break;
        CURL *c = new_curl(u, hl);
        k.sent = 0;
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_sock);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
        CURLcode rc = curl_easy_perform(c);
        long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        free(u);
        pos += k.sent;
        if (k.gone) break;
        if (rc != CURLE_OK || code >= 400) { if (++fails > 4) break; sleep(1); continue; }
        fails = 0;
        if (b < 0 && k.sent < SP_CHUNK) break;
    }
    curl_slist_free_all(hl);
}
static void serve_direct(int fd, sp_entry *e, long long a, long long b) {
    (void)a; (void)b;
    reply(fd, "200 OK", "Content-Type: audio/mpeg\r\n");
    sink k = {fd, 0, 0};
    CURL *c = new_curl(e->url, NULL);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_sock);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &k);
    curl_easy_perform(c);
    curl_easy_cleanup(c);
}
static void serve_hls(int fd, sp_entry *e) {
    (void)e;
    reply(fd, "200 OK", "Content-Type: video/mp2t\r\n");
    sink k = {fd, 0, 0};
    (void)k;
}
static void *conn_thread(void *arg) {
    int fd = (int)(long)arg;
    char req[8192]; int len = 0;
    while (len < (int)sizeof req - 1) {
        ssize_t r = recv(fd, req + len, sizeof req - 1 - len, 0);
        if (r <= 0) break;
        len += r; req[len] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[len] = 0;
    int id = 0; long long a = 0, b = -1;
    if (sscanf(req, "GET /s/%d", &id) == 1 || sscanf(req, "HEAD /s/%d", &id) == 1) {
        const char *r = strcasestr(req, "\nRange: bytes=");
        if (r) {
            r += 14; a = atoll(r);
            const char *dash = strchr(r, '-');
            if (dash && dash[1] >= '0' && dash[1] <= '9') b = atoll(dash + 1);
        }
    }
    sp_entry e = {0};
    if (!id || !find(id, &e)) reply(fd, "404 Not Found", "Content-Length: 0\r\n");
    else {
        if (e.kind == K_RANGE) serve_range(fd, &e, a, b);
        else if (e.kind == K_DIRECT) serve_direct(fd, &e, a, b);
        else serve_hls(fd, &e);
        free(e.url); free(e.referer);
    }
    shutdown(fd, SHUT_RDWR); close(fd);
    return NULL;
}
static void *accept_thread(void *arg) {
    (void)arg;
    for (;;) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; sleep(1); continue; }
        pthread_t t; pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&at, 256 * 1024);
        if (pthread_create(&t, &at, conn_thread, (void *)(long)fd)) close(fd);
        pthread_attr_destroy(&at);
    }
    return NULL;
}
int sp_port(void) { return port; }
int sp_start(void) {
    if (listen_fd >= 0) return port;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd, (struct sockaddr *)&sa, sizeof sa) || listen(listen_fd, 16)) {
        close(listen_fd); listen_fd = -1; return -1;
    }
    socklen_t sl = sizeof sa;
    getsockname(listen_fd, (struct sockaddr *)&sa, &sl);
    port = ntohs(sa.sin_port);
    pthread_t t;
    pthread_create(&t, NULL, accept_thread, NULL);
    pthread_detach(t);
    return port;
}
