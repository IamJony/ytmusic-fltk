#ifndef SANSON_BACKEND_H
#define SANSON_BACKEND_H
#include <functional>
#include <string>
#include <vector>

enum { P_MUSIC, P_COUNT };

struct Item {
    int plat = P_MUSIC;
    std::string kind;
    std::string id, title, author, author_url, author_thumb, thumb, url;
    std::string album, album_url, note;
    long long views = -1, date = 0, subs = -1;
    int duration = 0;
    bool live = false;
    bool is_playlist = false;
};

struct Page {
    int plat = P_MUSIC;
    int pager = 0;
    bool more = false;
    std::vector<Item> items;
    std::string error;
};

struct Source {
    std::string label, url, audio;
    int height = 0, kind = 0;
    long long vlen = 0, alen = 0;
};
enum { S_HLS, S_RANGE, S_DIRECT };

struct AudioTrack { std::string label, url; long long len = 0; };

struct Details {
    Item item;
    std::string description;
    std::vector<Source> sources;
    std::vector<AudioTrack> audios;
    std::vector<std::string> subtitles;
    std::vector<Item> related;
    std::string error;
    long long likes = -1;
    std::string lyrics_id;
    std::string fallback;
};

struct Lyrics {
    std::vector<std::pair<long long, std::string>> lines;
    std::string plain, source, error;
};

struct Channel {
    int plat = P_MUSIC;
    std::string name, url, thumb, banner, description;
    long long subs = -1;
    std::string error;
};

const char *plat_name(int plat);

void backend_start(const std::string &plugins_dir, const std::string &host_js);
void backend_home(std::function<void(Page)> cb);
void backend_search(const std::string &query, int filter, int start_index, std::function<void(Page)> cb);
void backend_next(int pager, std::function<void(Page)> cb);
void backend_drop(int pager);
void backend_details(const std::string &url, std::function<void(Details)> cb);
void backend_expand_playlist(const std::string &url, int start_index, std::function<void(std::vector<Item>)> cb);
void backend_channel(const std::string &url, std::function<void(Channel)> cb);
void backend_channel_videos(const std::string &url, std::function<void(Page)> cb);
void backend_lyrics(const Details &d, std::function<void(Lyrics)> cb);
void backend_radio(const std::string &url, std::function<void(Page)> cb);
void backend_reload();

void thumbs_fetch(const std::string &url, std::function<void(std::string)> cb);

std::string config_dir();
std::string read_file(const std::string &path);
void write_file(const std::string &path, const std::string &text, int mode = 0600);
std::string setting(const std::string &key, const std::string &def = "");
void set_setting(const std::string &key, const std::string &value);
std::vector<Item> music_queue_list();
void music_queue_save(const std::vector<Item> &v);
std::string fmt_count(long long n);
std::string fmt_duration(int secs);
std::string fmt_ago(long long epoch);

#endif
