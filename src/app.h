#ifndef SANSON_APP_H
#define SANSON_APP_H
#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/fl_ask.H>
#include <deque>
#include <set>
#include "views.h"
#include "data.h"
#include "player.h"

#define VERSION "0.1.0"  /* ytmusic-fltk */

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int w, int h, const char *l) : Fl_Double_Window(w, h, l) {}
    void resize(int X, int Y, int W, int H) override;
};

class PlaylistBrowser : public Fl_Hold_Browser {
public:
    PlaylistBrowser(int x, int y, int w, int h) : Fl_Hold_Browser(x, y, w, h) {}
    int handle(int e) override;
};

struct ViewState {
    std::vector<Item> view_items;
    std::vector<Item> queue;
    int qpos = -1;
    std::string current_playlist_name;
    std::string current_loading_playlist_url;
    int playlist_start_index = 0;
    int search_start_index = 0;
    std::string last_search_query;
    int last_search_filter = 0;
    std::vector<ViewState> view_stack;
    std::string load_more_label;
    bool load_more_visible = false;
    bool is_search = false;
    bool is_fav = false;
    bool is_dl = false;
    bool is_pl = false;
};

struct App {
    bool ready = false;
    MainWindow *win;
    Bar *top, *status_bar, *controls;
    Fl_Input *search;
    Fl_Choice* search_filter;
    Label *logo, *status, *time_label;
    Fl_Box *sidebar = nullptr;
    Label *lib_title = nullptr;
    Label *pl_title = nullptr;  /* sidebar izquierdo de biblioteca */
    LinkLabel *github_link = nullptr;
    QueueView *queue_view;
    FlatButton *play_btn, *prev_btn, *next_btn, *shuffle_btn, *repeat_btn, *radio_btn;
    VolumeBar *volume_bar;
    Label *vol_icon;
    Seek *seek;
    int view = 0;
    int watch_gen = 0;
    Details cur;
    int cur_source = -1;
    bool fullscreen = false;
    int fs_w = 0, fs_h = 0;

    std::vector<Item> queue, queue_orig, view_items;
    int qpos = -1;
    int repeat_mode = 0;
    bool shuffle = false, radio = true, end_handled = false;
    bool music_bg = false;
    bool is_loading = false;
    bool user_pick_pending = false;
    double last_play_at = 0.0;
    int pending_qpos = -1;          /* indice que el usuario pidio */
    int play_gen = 0;               /* generacion de reproduccion actual */
    bool is_downloading = false;
    MplayerWrapper mplayer;
    std::string current_playlist_name;
    std::string current_loading_playlist_url;
    int playlist_start_index = 0;
    
    FlatButton* fav_btn;
    FlatButton* downloads_btn;
    Fl_Hold_Browser* lib_browser;
    FlatButton* new_pl_btn;
    FlatButton* back_btn = nullptr;
    FlatButton* theme_btn = nullptr;
    FlatButton* load_more_btn;
    int search_start_index = 0;
    std::string last_search_query;
    int last_search_filter = 0;
    std::vector<ViewState> view_stack;

    void build();
    void layout();
    void refresh_all_colors();
    void refresh_library();
    void play_queue(int i);
    void open_item(const Item &it);
    void play_source(int idx, double start);
    void update_controls();
    void next_track(bool automatic);
    void prev_track();
    void set_shuffle(bool on);
    void music_buttons();
    void stop_watch();
    void set_status(const std::string &s, Fl_Color color = C_TEXT);
    void push_view_state(bool is_search=false, bool is_fav=false, bool is_dl=false, bool is_pl=false);
    void pop_view_state();
};

extern App *app;
void quit_app();
void watchdog_tick(void *);
#endif
