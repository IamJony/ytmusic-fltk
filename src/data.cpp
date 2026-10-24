#include "data.h"
#include "cJSON.h"
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <algorithm>

Library& Library::instance() {
    static Library lib;
    return lib;
}

static std::string get_data_path() {
    const char* home = getenv("HOME");
    std::string dir = std::string(home ? home : "/tmp") + "/.config/ytmusic-fltk";
    mkdir(dir.c_str(), 0700);
    return dir + "/data.json";
}

void Library::load() {
    favorites.clear();
    playlists.clear();
    std::ifstream f(get_data_path());
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) return;

    cJSON* favs = cJSON_GetObjectItemCaseSensitive(root, "favorites");
    cJSON* item;
    cJSON_ArrayForEach(item, favs) {
        cJSON* t = cJSON_GetObjectItem(item, "title");
        cJSON* u = cJSON_GetObjectItem(item, "url");
        if (t && t->valuestring && u && u->valuestring) {
            Item it;
            it.title = t->valuestring;
            it.url = u->valuestring;
            cJSON* th = cJSON_GetObjectItem(item, "thumb");
            if (th && th->valuestring) it.thumb = th->valuestring;
            favorites.push_back(it);
        }
    }

    cJSON* pls = cJSON_GetObjectItemCaseSensitive(root, "playlists");
    cJSON_ArrayForEach(item, pls) {
        if (!item->string) continue;
        std::string name = item->string;
        // Evitar duplicados en caso de JSON corrupto
        bool exists = false;
        for (auto& pl : playlists) if (pl.first == name) exists = true;
        if (exists) continue;

        std::vector<Item> items;
        cJSON* sub;
        cJSON_ArrayForEach(sub, item) {
            cJSON* t = cJSON_GetObjectItem(sub, "title");
            cJSON* u = cJSON_GetObjectItem(sub, "url");
            if (t && t->valuestring && u && u->valuestring) {
                Item it;
                it.title = t->valuestring;
                it.url = u->valuestring;
                cJSON* th = cJSON_GetObjectItem(sub, "thumb");
                if (th && th->valuestring) it.thumb = th->valuestring;
                items.push_back(it);
            }
        }
        playlists.push_back({name, items});
    }
    cJSON_Delete(root);
}

void Library::save() {
    cJSON* root = cJSON_CreateObject();
    cJSON* favs = cJSON_CreateArray();
    for (auto& it : favorites) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "title", it.title.c_str());
        cJSON_AddStringToObject(obj, "url", it.url.c_str());
        cJSON_AddStringToObject(obj, "thumb", it.thumb.c_str());
        cJSON_AddItemToArray(favs, obj);
    }
    cJSON_AddItemToObject(root, "favorites", favs);

    cJSON* pls = cJSON_CreateObject();
    for (auto& pl : playlists) {
        cJSON* arr = cJSON_CreateArray();
        for (auto& it : pl.second) {
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddStringToObject(obj, "title", it.title.c_str());
            cJSON_AddStringToObject(obj, "url", it.url.c_str());
            cJSON_AddStringToObject(obj, "thumb", it.thumb.c_str());
            cJSON_AddItemToArray(arr, obj);
        }
        cJSON_AddItemToObject(pls, pl.first.c_str(), arr);
    }
    cJSON_AddItemToObject(root, "playlists", pls);

    char* str = cJSON_PrintUnformatted(root);
    std::ofstream f(get_data_path());
    f << str;
    f.close();
    free(str);
    cJSON_Delete(root);
}

void Library::add_favorite(const Item& it) {
    if (!is_favorite(it.url)) {
        favorites.push_back(it);
        save();
    }
}

void Library::remove_favorite(const std::string& url) {
    favorites.erase(std::remove_if(favorites.begin(), favorites.end(),
        [&](const Item& i) { return i.url == url; }), favorites.end());
    save();
}

bool Library::is_favorite(const std::string& url) {
    for (auto& it : favorites) if (it.url == url) return true;
    return false;
}

void Library::add_to_playlist(const std::string& name, const Item& it) {
    for (auto& pl : playlists) {
        if (pl.first == name) {
            bool exists = false;
            for (auto& x : pl.second) if (x.url == it.url) exists = true;
            if (!exists) {
                pl.second.push_back(it);
                save();
            }
            return;
        }
    }
    // Si no existe, la crea
    playlists.push_back({name, {it}});
    save();
}

void Library::create_playlist(const std::string& name) {
    for (auto& pl : playlists) if (pl.first == name) return;
    playlists.push_back({name, {}});
    save();
}

void Library::rename_playlist(const std::string& old_name, const std::string& new_name) {
    for (auto& pl : playlists) {
        if (pl.first == old_name) {
            pl.first = new_name;
            save();
            return;
        }
    }
}

void Library::remove_playlist(const std::string& name) {
    playlists.erase(std::remove_if(playlists.begin(), playlists.end(),
        [&](const auto& p) { return p.first == name; }), playlists.end());
    save();
}

void Library::remove_from_playlist(const std::string& name, const std::string& url) {
    for (auto& pl : playlists) {
        if (pl.first == name) {
            pl.second.erase(std::remove_if(pl.second.begin(), pl.second.end(),
                [&](const Item& i) { return i.url == url; }), pl.second.end());
            save();
            return;
        }
    }
}
