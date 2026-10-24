#ifndef SANSON_PARSE_H
#define SANSON_PARSE_H
#include <string>
#include "backend.h"
#include "cJSON.h"

std::string jstr(cJSON *o, const char *k);
std::string decode_unicode(const std::string &s);
std::string clean_title(const std::string &s);
long long jnum(cJSON *o, const char *k, long long def = -1);
bool jbool(cJSON *o, const char *k);
std::string error_of(cJSON *root);
Item parse_item(cJSON *o, int plat);
Page parse_page(const char *json, int plat);
void parse_details(Details &d, cJSON *v);
void parse_music_audio(Details &m, cJSON *v);
cJSON *item_to_json(const Item &it);
Item item_from_json(cJSON *o);
#endif
