#ifndef SANSON_DATA_H
#define SANSON_DATA_H
#include <string>
#include <vector>
#include "backend.h"

struct Library {
    std::vector<Item> favorites;
    std::vector<std::pair<std::string, std::vector<Item>>> playlists;
    
    static Library& instance();
    void load();
    void save();
    void add_favorite(const Item& it);
    void remove_favorite(const std::string& url);
    bool is_favorite(const std::string& url);
    void add_to_playlist(const std::string& name, const Item& it);
    void create_playlist(const std::string& name);
    void remove_playlist(const std::string& name);
    void rename_playlist(const std::string& old_name, const std::string& new_name);
    void remove_from_playlist(const std::string& name, const std::string& url);
};
#endif
