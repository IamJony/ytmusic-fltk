/* Backend simplificado: solo Música. Usa el plugin de YouTube Music.
 * Si el plugin no está, cae en yt-dlp como proceso hijo. */
#include "backend.h"
#include <FL/Fl.H>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unistd.h>
#include "parse.h"
#include "stream.h"
#include "cJSON.h"
#include <cstring>

static const char *names[P_COUNT] = {"Música"};
const char *plat_name(int plat) { (void)plat; return names[0]; }

static std::string g_plugins, g_host;
static std::mutex g_lock;

void backend_start(const std::string &plugins_dir, const std::string &host_js) {
    g_plugins = plugins_dir; g_host = host_js;
    sp_start();
}

static void ui_call(std::function<void()> f) {
    auto *p = new std::function<void()>(std::move(f));
    Fl::awake([](void *d) {
        auto *q = static_cast<std::function<void()> *>(d);
        (*q)(); delete q;
    }, p);
}

// Stub: usa yt-dlp como fallback para búsqueda y detalles.
static std::string exec(const std::string &cmd) {
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    return out;
}

// cJSON-fixed-v1
// cJSON-fixed-v2 (ie_key-aware)
void backend_search(const std::string &query, int filter, int start_index, std::function<void(Page)> cb) {
    std::thread([query, filter, start_index, cb] {
        Page pg;
        std::string url = "https://www.youtube.com/results?search_query=";
        for (char c : query) { if (c == ' ') url += '+'; else url += c; }
        if (filter == 0) url += "&sp=EgIQAQ%3D%3D";
        else if (filter == 1) url += "&sp=EgIQAQ%3D%3D";
        else if (filter == 2) url += "&sp=EgIQAw%3D%3D";

        std::string cmd = "yt-dlp \"" + url + "\" --flat-playlist --dump-json --no-warnings 2>/dev/null";
        if (start_index > 0) {
            cmd += " --playlist-start " + std::to_string(start_index + 1);
            cmd += " --playlist-end "   + std::to_string(start_index + 15);
        } else {
            cmd += " --playlist-end 15";
        }
        std::string raw = exec(cmd);

        size_t pos = 0;
        while (pos < raw.size()) {
            size_t nl = raw.find('\n', pos);
            if (nl == std::string::npos) nl = raw.size();
            std::string line = raw.substr(pos, nl - pos);
            pos = nl + 1;
            if (line.empty()) continue;

            cJSON *o = cJSON_Parse(line.c_str());
            if (!o) continue;

            cJSON *jt   = cJSON_GetObjectItem(o, "title");
            cJSON *ji   = cJSON_GetObjectItem(o, "id");
            cJSON *jtyp = cJSON_GetObjectItem(o, "_type");
            cJSON *jie  = cJSON_GetObjectItem(o, "ie_key");
            cJSON *jur  = cJSON_GetObjectItem(o, "url");
            cJSON *jweb = cJSON_GetObjectItem(o, "webpage_url");

            if (cJSON_IsString(jt) && cJSON_IsString(ji)) {
                Item it;
                it.id    = ji->valuestring;
                it.title = decode_unicode(jt->valuestring);

                std::string typ = cJSON_IsString(jtyp) ? jtyp->valuestring : "";
                std::string ie  = cJSON_IsString(jie)  ? jie->valuestring  : "";

                bool is_playlist = false;
                std::string real_url;

                if (ie == "YoutubeTab" || ie == "YoutubePlaylist" || typ == "playlist") {
                    is_playlist = true;
                    if (cJSON_IsString(jur) && jur->valuestring && !strncmp(jur->valuestring,"http",4))
                        real_url = jur->valuestring;
                    else if (cJSON_IsString(jweb) && jweb->valuestring && !strncmp(jweb->valuestring,"http",4))
                        real_url = jweb->valuestring;
                    else
                        real_url = "https://www.youtube.com/playlist?list=" + it.id;
                } else {
                    is_playlist = false;
                    if (cJSON_IsString(jur) && jur->valuestring && !strncmp(jur->valuestring,"http",4))
                        real_url = jur->valuestring;
                    else
                        real_url = "https://www.youtube.com/watch?v=" + it.id;
                }

                it.kind = is_playlist ? "playlist" : "video";
                it.is_playlist = is_playlist;
                it.url = real_url;

                fprintf(stderr, "[search] title='%s' id=%s type='%s' ie='%s' is_pl=%d\n",
                        it.title.c_str(), it.id.c_str(), typ.c_str(), ie.c_str(),
                        (int)is_playlist);
                pg.items.push_back(it);
            }
            cJSON_Delete(o);
        }
        ui_call([cb, pg] { cb(pg); Fl::check(); });
    }).detach();
}



void backend_expand_playlist(const std::string &url, int start_index, std::function<void(std::vector<Item>)> cb) {
    std::thread([url, start_index, cb] {
        std::vector<Item> items;
        std::string cmd = "yt-dlp \"" + url + "\" --flat-playlist --lazy-playlist --skip-download --dump-json --no-warnings 2>/dev/null";
        if (start_index > 0) {
            cmd += " --playlist-start " + std::to_string(start_index + 1);
            cmd += " --playlist-end "   + std::to_string(start_index + 15);
        } else {
            cmd += " --playlist-end 15";
        }
        std::string raw = exec(cmd);

        fprintf(stderr, "[expand] url=%s raw_len=%zu\n", url.c_str(), raw.size());

        size_t pos = 0;
        while (pos < raw.size()) {
            size_t nl = raw.find('\n', pos);
            if (nl == std::string::npos) nl = raw.size();
            std::string line = raw.substr(pos, nl - pos);
            pos = nl + 1;
            if (line.empty()) continue;

            cJSON *o = cJSON_Parse(line.c_str());
            if (!o) continue;

            cJSON *jt   = cJSON_GetObjectItem(o, "title");
            cJSON *ji   = cJSON_GetObjectItem(o, "id");
            cJSON *jur  = cJSON_GetObjectItem(o, "url");
            cJSON *jweb = cJSON_GetObjectItem(o, "webpage_url");

            if (cJSON_IsString(jt) && cJSON_IsString(ji)) {
                Item it;
                it.kind  = "video";
                it.id    = ji->valuestring;
                it.title = decode_unicode(jt->valuestring);
                if (cJSON_IsString(jweb) && jweb->valuestring && !strncmp(jweb->valuestring,"http",4))
                    it.url = jweb->valuestring;
                else if (cJSON_IsString(jur) && jur->valuestring && !strncmp(jur->valuestring,"http",4))
                    it.url = jur->valuestring;
                else
                    it.url = "https://www.youtube.com/watch?v=" + it.id;
                items.push_back(it);
            }
            cJSON_Delete(o);
        }
        fprintf(stderr, "[expand] -> %zu items\n", items.size());
        ui_call([cb, items] { cb(items); });
    }).detach();
}



void backend_details(const std::string &url, std::function<void(Details)> cb) {
    std::thread([url, cb] {
        Details d;
        d.item.url = url;
        d.item.kind = "video";
        std::string cmd = "yt-dlp -g -f bestaudio \"" + url + "\" 2>/dev/null";
        std::string raw = exec(cmd);
        if (!raw.empty() && raw.back() == '\n') raw.pop_back();
        if (raw.empty()) {
            d.error = "No se pudo obtener el stream";
        } else {
            Source s;
            s.kind = S_RANGE;
            s.url = raw;
            s.label = "Audio";
            d.sources.push_back(s);
        }
        ui_call([cb, d] { cb(d); });
    }).detach();
}

void backend_home(std::function<void(Page)> cb) {
    Page pg; ui_call([cb, pg] { cb(pg); Fl::check(); });
}
void backend_next(int pager, std::function<void(Page)> cb) {
    (void)pager; Page pg; ui_call([cb, pg] { cb(pg); Fl::check(); });
}
void backend_drop(int pager) { (void)pager; }
void backend_channel(const std::string &url, std::function<void(Channel)> cb) {
    (void)url; Channel c; ui_call([cb, c] { cb(c); });
}
void backend_channel_videos(const std::string &url, std::function<void(Page)> cb) {
    (void)url; Page pg; ui_call([cb, pg] { cb(pg); Fl::check(); });
}
void backend_lyrics(const Details &d, std::function<void(Lyrics)> cb) {
    (void)d; Lyrics l; l.error = "Sin letra"; ui_call([cb, l] { cb(l); });
}
void backend_radio(const std::string &url, std::function<void(Page)> cb) {
    (void)url; Page pg; ui_call([cb, pg] { cb(pg); Fl::check(); });
}
void backend_reload() {}

void thumbs_fetch(const std::string &url, std::function<void(std::string)> cb) {
    (void)url; cb("");
}

std::string config_dir() {
    const char *h = getenv("HOME");
    std::string d = std::string(h ? h : "/tmp") + "/.config/ytmusic-fltk";
    mkdir(d.c_str(), 0700);
    return d;
}
std::string read_file(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::string s(n, 0);
    if (fread(&s[0], 1, n, f) != (size_t)n) s.clear();
    fclose(f);
    return s;
}
void write_file(const std::string &path, const std::string &text, int mode) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f); chmod(path.c_str(), mode);
}
std::string setting(const std::string &key, const std::string &def) {
    (void)key; return def;
}
void set_setting(const std::string &key, const std::string &value) { (void)key; (void)value; }
std::vector<Item> music_queue_list() { return {}; }
void music_queue_save(const std::vector<Item> &v) { (void)v; }

std::string fmt_count(long long n) {
    char b[32];
    if (n < 0) return "";
    if (n >= 1000000) snprintf(b, sizeof b, "%.1f M", n / 1e6);
    else if (n >= 1000) snprintf(b, sizeof b, "%.1f k", n / 1e3);
    else snprintf(b, sizeof b, "%lld", n);
    return b;
}
std::string fmt_duration(int secs) {
    char b[32];
    if (secs <= 0) return "";
    if (secs >= 3600) snprintf(b, sizeof b, "%d:%02d:%02d", secs / 3600, secs / 60 % 60, secs % 60);
    else snprintf(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
    return b;
}
std::string fmt_ago(long long epoch) { (void)epoch; return ""; }
