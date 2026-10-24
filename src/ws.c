/* WebSocket mínimo sobre libcurl. Copiado de Sanson. */
#define _GNU_SOURCE
#include "ws.h"
#include <curl/curl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "cJSON.h"

#define WS_MAX 16
#define DBG(...) do { if (getenv("WS_DEBUG")) { fprintf(stderr, "[ws] " __VA_ARGS__); fputc('\n', stderr); } } while (0)

typedef struct msg { char *text; size_t len; struct msg *next; } msg;
typedef struct {
    int id; int used; int closing; int closed; int refs;
    char *url; char *headers;
    msg *in_head, *in_tail;
    msg *out_head, *out_tail;
    pthread_t thread;
} ws_conn;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ws_conn conns[WS_MAX];
static int next_id = 1;

static void push(msg **head, msg **tail, const char *text, size_t len) {
    msg *m = calloc(1, sizeof *m);
    m->text = malloc(len + 1);
    memcpy(m->text, text, len); m->text[len] = 0; m->len = len;
    if (*tail) (*tail)->next = m; else *head = m;
    *tail = m;
}
static void free_list(msg *m) {
    while (m) { msg *n = m->next; free(m->text); free(m); m = n; }
}
static ws_conn *find(int id) {
    for (int i = 0; i < WS_MAX; i++)
        if (conns[i].used && conns[i].id == id) return &conns[i];
    return NULL;
}
static void release(ws_conn *c) {
    if (--c->refs > 0) return;
    free(c->url); free(c->headers);
    free_list(c->in_head); free_list(c->out_head);
    memset(c, 0, sizeof *c);
}

static int send_all(CURL *h, curl_socket_t fd, const unsigned char *p, size_t n) {
    while (n) {
        size_t sent = 0;
        CURLcode rc = curl_easy_send(h, p, n, &sent);
        if (rc == CURLE_AGAIN) {
            struct pollfd pf = {fd, POLLOUT, 0};
            poll(&pf, 1, 1000);
            continue;
        }
        if (rc != CURLE_OK) return -1;
        p += sent; n -= sent;
    }
    return 0;
}

static int send_frame(CURL *h, curl_socket_t fd, int opcode, const char *data, size_t len) {
    unsigned char head[14]; size_t hl = 0;
    head[hl++] = 0x80 | opcode;
    if (len < 126) head[hl++] = 0x80 | len;
    else if (len < 65536) { head[hl++] = 0x80 | 126; head[hl++] = len >> 8; head[hl++] = len & 0xff; }
    else { head[hl++] = 0x80 | 127; for (int i = 7; i >= 0; i--) head[hl++] = i >= 4 ? 0 : (len >> (i * 8)) & 0xff; }
    unsigned char mask[4]; for (int i = 0; i < 4; i++) mask[i] = rand() & 0xff;
    memcpy(head + hl, mask, 4); hl += 4;
    unsigned char *buf = malloc(hl + len);
    memcpy(buf, head, hl);
    for (size_t i = 0; i < len; i++) buf[hl + i] = data[i] ^ mask[i & 3];
    int r = send_all(h, fd, buf, hl + len);
    free(buf); return r;
}

static void base64(const unsigned char *in, int n, char *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63]; out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

typedef struct { unsigned char *data; size_t len, cap; } buf_t;
static void buf_add(buf_t *b, const void *p, size_t n) {
    if (b->len + n + 1 > b->cap) { b->cap = (b->len + n + 1) * 2; b->data = realloc(b->data, b->cap); }
    memcpy(b->data + b->len, p, n); b->len += n; b->data[b->len] = 0;
}
static void buf_drop(buf_t *b, size_t n) { memmove(b->data, b->data + n, b->len - n); b->len -= n; }

static int read_some(CURL *h, curl_socket_t fd, buf_t *b, int wait_ms) {
    unsigned char tmp[16384]; size_t got = 0;
    CURLcode rc = curl_easy_recv(h, tmp, sizeof tmp, &got);
    if (rc == CURLE_AGAIN) {
        struct pollfd pf = {fd, POLLIN, 0};
        if (poll(&pf, 1, wait_ms) <= 0) return 0;
        rc = curl_easy_recv(h, tmp, sizeof tmp, &got);
        if (rc == CURLE_AGAIN) return 0;
    }
    if (rc != CURLE_OK || got == 0) return -1;
    buf_add(b, tmp, got); return 1;
}

static int handshake(CURL *h, curl_socket_t fd, ws_conn *c, const char *host, const char *path, buf_t *in) {
    unsigned char key[16]; char key64[32];
    for (int i = 0; i < 16; i++) key[i] = rand() & 0xff;
    base64(key, 16, key64);
    char *req = NULL; size_t rl = 0;
    FILE *f = open_memstream(&req, &rl);
    fprintf(f, "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n", path, host, key64);
    int has_ua = 0, has_origin = 0;
    cJSON *hj = cJSON_Parse(c->headers ? c->headers : "{}");
    cJSON *it; cJSON_ArrayForEach(it, hj) {
        if (!cJSON_IsString(it) || !it->string) continue;
        if (!strcasecmp(it->string, "user-agent")) has_ua = 1;
        if (!strcasecmp(it->string, "origin")) has_origin = 1;
        fprintf(f, "%s: %s\r\n", it->string, it->valuestring);
    }
    cJSON_Delete(hj);
    if (!has_ua) fprintf(f, "User-Agent: Mozilla/5.0\r\n");
    if (!has_origin) fprintf(f, "Origin: https://%s\r\n", host);
    fprintf(f, "\r\n"); fclose(f);
    int r = send_all(h, fd, (unsigned char *)req, rl);
    free(req); if (r) return -1;
    time_t start = time(NULL);
    while (!in->data || !strstr((char *)in->data, "\r\n\r\n")) {
        if (read_some(h, fd, in, 500) < 0 || time(NULL) - start > 15) return -1;
    }
    char *end = strstr((char *)in->data, "\r\n\r\n");
    if (strncmp((char *)in->data + 8, " 101", 4)) return -1;
    buf_drop(in, end + 4 - (char *)in->data);
    return 0;
}

static void *run(void *arg) {
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5);
    ws_conn *c = arg;
    const char *u = c->url;
    int tls = !strncmp(u, "wss://", 6);
    const char *rest = u + (tls ? 6 : 5);
    const char *slash = strchr(rest, '/');
    const char *q = strchr(rest, '?');
    if (q && (!slash || q < slash)) slash = q;
    char host[512]; size_t hl = slash ? (size_t)(slash - rest) : strlen(rest);
    if (hl >= sizeof host) hl = sizeof host - 1;
    memcpy(host, rest, hl); host[hl] = 0;
    char *path = NULL;
    if (!slash) path = strdup("/");
    else if (*slash == '?') asprintf(&path, "/%s", slash);
    else path = strdup(slash);
    char *http_url = NULL;
    asprintf(&http_url, "%s://%s/", tls ? "https" : "http", host);

    CURL *h = curl_easy_init();
    curl_easy_setopt(h, CURLOPT_URL, http_url);
    curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(h, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    buf_t in = {0}, frag = {0}; int frag_op = 0;
    curl_socket_t fd = CURL_SOCKET_BAD;
    int ok = curl_easy_perform(h) == CURLE_OK
          && curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &fd) == CURLE_OK
          && fd != CURL_SOCKET_BAD
          && handshake(h, fd, c, host, path, &in) == 0;
    while (ok) {
        pthread_mutex_lock(&lock);
        int closing = c->closing;
        msg *out = c->out_head; c->out_head = c->out_tail = NULL;
        pthread_mutex_unlock(&lock);
        for (msg *m = out; m && ok; m = m->next)
            ok = send_frame(h, fd, 1, m->text, m->len) == 0;
        free_list(out);
        if (closing) { send_frame(h, fd, 8, "\x03\xe8", 2); break; }
        while (ok) {
            if (in.len >= 2) {
                unsigned char *d = in.data;
                int fin = d[0] & 0x80, op = d[0] & 0x0f;
                unsigned long long len = d[1] & 0x7f;
                size_t off = 2;
                if (len == 126) {
                    if (in.len < 4) goto need;
                    len = d[2] << 8 | d[3]; off = 4;
                } else if (len == 127) {
                    if (in.len < 10) goto need;
                    len = 0;
                    for (int i = 2; i < 10; i++) len = len << 8 | d[i];
                    off = 10;
                }
                if (d[1] & 0x80) off += 4;
                if (in.len < off + len) goto need;
                unsigned char *payload = d + off;
                if (d[1] & 0x80)
                    for (unsigned long long i = 0; i < len; i++)
                        payload[i] ^= d[off - 4 + (i & 3)];
                if (op == 9) ok = send_frame(h, fd, 10, (char *)payload, len) == 0;
                else if (op == 8) ok = 0;
                else if (op == 1 || op == 2 || op == 0) {
                    if (op) frag_op = op;
                    buf_add(&frag, payload, len);
                    if (fin) {
                        pthread_mutex_lock(&lock);
                        push(&c->in_head, &c->in_tail, (char *)frag.data, frag.len);
                        pthread_mutex_unlock(&lock);
                        frag.len = 0; frag_op = 0;
                    }
                }
                buf_drop(&in, off + len);
                continue;
            }
        need:
            break;
        }
        if (!ok) break;
        if (read_some(h, fd, &in, 200) < 0) ok = 0;
    }
    (void)frag_op;
    free(in.data); free(frag.data); free(path); free(http_url);
    curl_easy_cleanup(h);
    pthread_mutex_lock(&lock);
    c->closed = 1; release(c);
    pthread_mutex_unlock(&lock);
    return NULL;
}

int ws_open(const char *url, const char *headers_json) {
    if (strncmp(url, "wss://", 6) && strncmp(url, "ws://", 5)) return 0;
    pthread_mutex_lock(&lock);
    ws_conn *c = NULL;
    for (int i = 0; i < WS_MAX && !c; i++)
        if (!conns[i].used) c = &conns[i];
    if (!c) { pthread_mutex_unlock(&lock); return 0; }
    memset(c, 0, sizeof *c);
    c->used = 1; c->id = next_id++; c->refs = 2;
    c->url = strdup(url);
    c->headers = headers_json ? strdup(headers_json) : NULL;
    int id = c->id;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 256 * 1024);
    if (pthread_create(&c->thread, &at, run, c)) {
        c->closed = 1; c->refs = 1;
    }
    pthread_attr_destroy(&at);
    pthread_mutex_unlock(&lock);
    return id;
}

void ws_send(int id, const char *text) {
    pthread_mutex_lock(&lock);
    ws_conn *c = find(id);
    if (c && !c->closed && !c->closing)
        push(&c->out_head, &c->out_tail, text, strlen(text));
    pthread_mutex_unlock(&lock);
}

void ws_close(int id) {
    pthread_mutex_lock(&lock);
    ws_conn *c = find(id);
    if (c && !c->closing) { c->closing = 1; release(c); }
    pthread_mutex_unlock(&lock);
}

char *ws_poll(int id) {
    cJSON *r = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(r, "messages", arr);
    int closed = 1;
    pthread_mutex_lock(&lock);
    ws_conn *c = find(id);
    if (c) {
        for (msg *m = c->in_head; m; m = m->next)
            cJSON_AddItemToArray(arr, cJSON_CreateString(m->text));
        free_list(c->in_head); c->in_head = c->in_tail = NULL;
        closed = c->closed || c->closing;
        if (c->closed && !c->closing) { c->closing = 1; release(c); }
    }
    pthread_mutex_unlock(&lock);
    cJSON_AddBoolToObject(r, "closed", closed);
    char *s = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return s;
}
