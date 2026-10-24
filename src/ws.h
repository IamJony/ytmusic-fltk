#ifndef SANSON_WS_H
#define SANSON_WS_H
#ifdef __cplusplus
extern "C" {
#endif
int ws_open(const char *url, const char *headers_json);
void ws_send(int id, const char *text);
void ws_close(int id);
char *ws_poll(int id);
#ifdef __cplusplus
}
#endif
#endif
