#include "parse.h"
#include <cstring>
#include "stream.h"

std::string jstr(cJSON *o, const char *k) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
long long jnum(cJSON *o, const char *k, long long def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsNumber(v)) return (long long)v->valuedouble;
    return def;
}
bool jbool(cJSON *o, const char *k) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsTrue(v);
}
std::string error_of(cJSON *root) {
    if (!root) return "Respuesta inválida";
    if (!jbool(root, "__error")) return "";
    return jstr(root, "message");
}
Item parse_item(cJSON *o, int plat) {
    Item it; it.plat = plat;
    it.title = jstr(o, "name");
    it.url = jstr(o, "url");
    it.thumb = jstr(o, "thumbnail");
    it.duration = (int)jnum(o, "duration", 0);
    it.kind = "video";
    return it;
}
Page parse_page(const char *json, int plat) {
    Page pg; pg.plat = plat;
    cJSON *root = cJSON_Parse(json ? json : "");
    pg.error = error_of(root);
    if (pg.error.empty()) {
        pg.pager = (int)jnum(root, "__pager", 0);
        pg.more = jbool(root, "hasMore");
        cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "results");
        cJSON *o; cJSON_ArrayForEach(o, list) {
            Item it = parse_item(o, plat);
            if (!it.url.empty()) pg.items.push_back(it);
        }
    }
    cJSON_Delete(root);
    return pg;
}
void parse_details(Details &d, cJSON *v) {
    d.item = parse_item(v, P_MUSIC);
    d.description = jstr(v, "description");
}
void parse_music_audio(Details &m, cJSON *v) {
    (void)m; (void)v;
}
cJSON *item_to_json(const Item &it) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "title", cJSON_CreateString(it.title.c_str()));
    cJSON_AddItemToObject(o, "url", cJSON_CreateString(it.url.c_str()));
    cJSON_AddItemToObject(o, "thumb", cJSON_CreateString(it.thumb.c_str()));
    cJSON_AddItemToObject(o, "duration", cJSON_CreateNumber(it.duration));
    return o;
}
Item item_from_json(cJSON *o) {
    Item it;
    it.title = jstr(o, "title");
    it.url = jstr(o, "url");
    it.thumb = jstr(o, "thumb");
    it.duration = (int)jnum(o, "duration", 0);
    it.kind = "video";
    return it;
}

// Convierte secuencias \u00f3 a caracteres UTF-8 reales
std::string decode_unicode(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 5 < s.size() && s[i+1] == 'u') {
            char hex[5] = { s[i+2], s[i+3], s[i+4], s[i+5], 0 };
            unsigned int cp = strtol(hex, nullptr, 16);
            if (cp <= 0x7F) {
                out += (char)cp;
            } else if (cp <= 0x7FF) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            i += 5;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string clean_title(const std::string &s) {
    std::string r = s;
    std::vector<std::string> basura = {" (Official Video)", " (Official Audio)", 
                                       " (Official Visualizer)", " (Video Oficial)",
                                       " [Official Video]", " (Audio)", " (En Vivo)", " [En Vivo]"};
    for (const auto &b : basura) {
        size_t pos;
        while ((pos = r.find(b)) != std::string::npos) r.erase(pos, b.length());
    }
    return r;
}
