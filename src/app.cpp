#include "app.h"
#include "themes.h"
#include <FL/filename.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/x.H>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>

void App::set_status(const std::string &s, Fl_Color color) {
    status->set(s);
    if (!is_loading) {
        status->fg = color;
        status->show();
        status->redraw();
    }
}

static void blink_status(void*) {
    if (!app || !app->status) return;
    if (app->is_loading || app->is_downloading) {
        if (app->status->visible()) app->status->hide();
        else app->status->show();
        app->status->redraw();
    } else {
        app->status->show();
        app->status->redraw();
    }
    Fl::repeat_timeout(0.5, blink_status);
}

App *app;
static bool showed_song_title = false;

void quit_app() { app->mplayer.stop(); fflush(nullptr); _exit(0); }

static void update_timer(void *) {
    if (app && app->cur_source >= 0 && !app->is_loading) {
        app->update_controls();
    }
    Fl::repeat_timeout(0.5, update_timer);
}

static void on_mpv_event(int event_id) {
    if (event_id != EVT_END_FILE) return;
    int gen = app ? app->play_gen : 0;
    Fl::awake([](void* p) {
        int ev_gen = (int)(intptr_t)p;
        if (!app) return;
        if (ev_gen != app->play_gen) return;
        if (app->cur_source < 0) return;
        app->user_pick_pending = false;
        app->pending_qpos = -1;
        app->next_track(true);
    }, (void*)(intptr_t)gen);
}

static double probe_duration_real(const std::string &path) {
    std::string cmd = "ffmpeg -i '" + path + "' -af silencedetect=noise=-40dB:d=1 "
                      "-f null - 2>&1 | grep 'silence_start' | tail -1 | "
                      "sed 's/.*silence_start: //' | cut -d' ' -f1";
    FILE* p = popen(cmd.c_str(), "r");
    double dur = 0.0;
    if (p) {
        char buf[64];
        if (fgets(buf, sizeof buf, p)) dur = atof(buf);
        pclose(p);
    }
    if (dur > 1.0) return dur;

    std::string cmd2 = "ffmpeg -v quiet -stats -i '" + path + "' -f null - 2>&1 "
                       "| grep -o 'time=[0-9:.]*' | tail -1 | cut -d= -f2";
    FILE* p2 = popen(cmd2.c_str(), "r");
    if (p2) {
        char buf[64];
        if (fgets(buf, sizeof buf, p2)) {
            int h = 0, m = 0;
            double s = 0.0;
            if (sscanf(buf, "%d:%d:%lf", &h, &m, &s) == 3) {
                dur = h * 3600.0 + m * 60.0 + s;
            } else if (sscanf(buf, "%d:%lf", &m, &s) == 2) {
                dur = m * 60.0 + s;
            }
        }
        pclose(p2);
    }
    if (dur <= 0.0) {
        std::string cmd3 = "ffprobe -v error -show_entries format=duration "
                           "-of default=noprint_wrappers=1:nokey=1 '" + path + "' 2>/dev/null";
        FILE* p3 = popen(cmd3.c_str(), "r");
        if (p3) {
            char buf[64];
            if (fgets(buf, sizeof buf, p3)) dur = atof(buf);
            pclose(p3);
        }
    }
    return dur;
}

static std::string get_exe_dir() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    std::string p(buf);
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return ".";
    return p.substr(0, pos);
}

static void setup_qjs_env() {
    std::string exe_dir = get_exe_dir();
    std::vector<std::string> candidates = {
        exe_dir + "/qjs",
        exe_dir + "/bin/qjs",
        exe_dir + "/../bin/qjs",
        exe_dir + "/../lib/ytmusic-fltk/qjs",
        exe_dir + "/quickjs",
    };
    std::string qjs_path;
    for (const auto& c : candidates) {
        if (access(c.c_str(), X_OK) == 0) { qjs_path = c; break; }
    }
    if (qjs_path.empty()) return;

    std::string qjs_dir = qjs_path.substr(0, qjs_path.find_last_of('/'));
    const char* old_path = getenv("PATH");
    std::string new_path = qjs_dir + ":";
    if (old_path) new_path += old_path;
    setenv("PATH", new_path.c_str(), 1);
    setenv("QJS_PATH", qjs_path.c_str(), 1);
}

void App::refresh_all_colors() {
    if (!win) return;
    win->color(C_BG);
    if (search) style_input(search);
    if (search_filter) { search_filter->color(C_BTN); search_filter->textcolor(C_TEXT); }
    if (lib_browser) {
        lib_browser->color(C_SIDE);
        lib_browser->textcolor(C_TEXT);
        lib_browser->selection_color(C_SEL);
    }
    if (new_pl_btn) { new_pl_btn->bg = C_ACCENT; new_pl_btn->fg = C_BG; }
    if (sidebar) sidebar->color(C_SIDE);
    if (github_link) github_link->redraw();
    win->redraw();
    Fl::flush();
}

void App::layout() {
    int W = win->w(), H = win->h();
    top->resize(0, 0, W, 50);
    logo->resize(15, 0, 240, 50);
    if (back_btn) back_btn->resize(W - 200, 10, 90, 30);
    if (theme_btn) theme_btn->resize(W - 105, 10, 95, 30);
    if (search_filter) search_filter->resize(W - 360, 10, 150, 30);
    search->resize(170, 10, W - 540, 30);
    status->resize(10, H - 20, W - 20, 20);

    const int SB_W = 200;
    const int PAD  = 10;
    const int INNER_W = SB_W - 2*PAD;

    if (sidebar) sidebar->resize(0, 50, SB_W, H - 150);
    if (lib_title) lib_title->resize(PAD, 60, INNER_W, 20);
    if (fav_btn) fav_btn->resize(PAD, 85, INNER_W, 25);
    if (downloads_btn) downloads_btn->resize(PAD, 113, INNER_W, 25);
    if (pl_title) pl_title->resize(PAD, 150, INNER_W - 60, 20);
    if (new_pl_btn) new_pl_btn->resize(SB_W - PAD - 50, 150, 50, 20);
    if (lib_browser) lib_browser->resize(PAD, 175, INNER_W, H - 175 - 120);

    queue_view->resize(200, 50, W - 200, H - 180);
    load_more_btn->resize(200, H - 130, W - 200, 30);
    controls->resize(0, H - 100, W, 100);
    seek->resize(10, H - 95, W - 20, 20);

    int by = H - 65;
    prev_btn->resize(20, by, 40, 30);
    play_btn->resize(65, by, 40, 30);
    next_btn->resize(110, by, 40, 30);
    time_label->resize(160, by, 120, 30);
    shuffle_btn->resize(W - 260, by, 40, 30);
    repeat_btn->resize(W - 215, by, 40, 30);
    if (vol_icon) vol_icon->resize(W - 170, by, 30, 30);
    volume_bar->resize(W - 135, by + 10, 120, 10);

    status_bar->resize(0, H - 20, W, 20);
    status->resize(10, H - 20, W - 160, 20);
    if (github_link) github_link->resize(W - 150, H - 20, 140, 20);

    win->redraw();
}

void MainWindow::resize(int X, int Y, int W, int H) {
    bool sized = W != w() || H != h();
    Fl_Double_Window::resize(X, Y, W, H);
    if (sized && app && app->ready) app->layout();
}

int PlaylistBrowser::handle(int e) {
    if (e == FL_PUSH && Fl::event_button() == FL_RIGHT_MOUSE) {
        int line = this->value();
        if (line > 0) {
            const char* txt = this->text(line);
            if (txt) {
                std::string full_text = txt;
                size_t pos = full_text.rfind(" (");
                std::string name = (pos != std::string::npos) ? full_text.substr(0, pos) : full_text;
                Fl_Menu_Item menu[] = {
                    {"Renombrar Playlist", 0, 0, 0, 0, 0, 0, 0, 0},
                    {"Eliminar Playlist", 0, 0, 0, 0, 0, 0, 0, 0},
                    {0, 0, 0, 0, 0, 0, 0, 0, 0}
                };
                Fl_Menu_Button mb(0, 0, 0, 0); mb.color(C_PANEL); mb.textcolor(C_TEXT); mb.selection_color(C_SEL); const Fl_Menu_Item* m = menu->popup(Fl::event_x(), Fl::event_y(), 0, 0, &mb);
                if (m) {
                    int idx = (int)(m - menu);
                    if (idx == 0) {
                        const char* new_name = fl_input("Nuevo nombre:", name.c_str());
                        if (new_name && *new_name && name != new_name) {
                            Library::instance().rename_playlist(name, new_name);
                            if (app && app->current_playlist_name == name) {
                                app->current_playlist_name = new_name;
                                app->set_status("Playlist renombrada a: " + std::string(new_name));
                            }
                            if (app) app->refresh_library();
                        }
                    } else if (idx == 1) {
                        int confirm = fl_choice("¿Eliminar la playlist '%s'?", "Cancelar", "Eliminar", nullptr, name.c_str());
                        if (confirm == 1) {
                            Library::instance().remove_playlist(name);
                            if (app && app->current_playlist_name == name) {
                                app->queue.clear();
                                app->current_playlist_name.clear();
                                app->queue_view->current = -1;
                                app->queue_view->redraw();
                                app->set_status("Playlist eliminada");
                            }
                            if (app) app->refresh_library();
                        }
                    }
                }
            }
        }
        return 1;
    }
    return Fl_Hold_Browser::handle(e);
}

void App::build() {
    int W = 900, H = 600;
    win = new MainWindow(W, H, "ytmusic-fltk");
    win->color(C_BG);
    win->size_range(700, 500);

    top = new Bar(0, 0, W, 50, 0);
    logo = new Label(15, 0, 240, 50);
    logo->size = 18;
    logo->fg_role = 2;
    logo->set("ytmusic-fltk");

    search = new Fl_Input(170, 10, W - 440, 30);
    style_input(search);
    search->when(FL_WHEN_ENTER_KEY_ALWAYS);

    search_filter = new Fl_Choice(W - 260, 10, 100, 30);
    search_filter->add("Canciones|Videos|Playlists");
    search_filter->value(0);
    search_filter->color(C_BTN);
    search_filter->textcolor(C_TEXT);
    search_filter->textfont(FL_HELVETICA_BOLD);
    search_filter->textsize(12);
    search_filter->box(FL_FLAT_BOX);

    search->callback([](Fl_Widget *, void *) {
        const char *q = app->search->value();
        if (!q || !*q) return;
        int filter = app->search_filter->value();
        app->push_view_state(true);
        app->last_search_query = q;
        app->last_search_filter = filter;
        app->search_start_index = 0;
        app->load_more_btn->label("Cargar más resultados");
        app->load_more_btn->activate();
        app->load_more_btn->hide();
        app->current_loading_playlist_url.clear();
        app->playlist_start_index = 0;
        app->set_status("Buscando…", C_GREEN);
        app->is_loading = true;
        Fl::add_timeout(0.5, blink_status);
        backend_search(q, filter, 0, [](Page pg) {
            app->view_items.clear();
            for (auto &it : pg.items) app->view_items.push_back(it);
            app->queue_view->current = -1;
            app->is_loading = false;
            app->set_status(std::to_string(pg.items.size()) + " resultados");
            app->queue_view->redraw();
            app->win->redraw();
            if (!pg.items.empty()) {
                app->load_more_btn->show();
                app->layout();
            }
        });
    });

    back_btn = new FlatButton(W - 190, 10, 80, 30, "VOLVER");
    back_btn->callback([](Fl_Widget*, void*) {
        if (app) app->pop_view_state();
    });

    theme_btn = new FlatButton(W - 100, 10, 90, 30, "Temas");
    theme_btn->callback([](Fl_Widget*, void*) {
        Fl_Menu_Item menu[32];
        for (int i = 0; i < theme_count; i++) {
            menu[i] = {themes[i].name, 0, 0, (void*)(intptr_t)i, 0, 0, 0, 0, 0};
        }
        menu[theme_count] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        const Fl_Menu_Item* m = menu->popup(Fl::event_x(), Fl::event_y());
        if (m) {
            int idx = (int)(intptr_t)m->user_data();
            apply_theme(idx);
            save_theme(idx);
            app->set_status("Tema: " + std::string(themes[idx].name));
        }
    });

    top->end();

    sidebar = new Fl_Box(0, 50, 200, H - 150);
    sidebar->box(FL_FLAT_BOX);
    sidebar->color(C_SIDE);

    lib_title = new Label(10, 60, 180, 20);
    lib_title->size = 14; lib_title->fg_role = 2; lib_title->set("BIBLIOTECA");

    fav_btn = new FlatButton(10, 85, 180, 25, "Favoritos");
    fav_btn->bg = C_BTN;
    fav_btn->callback([](Fl_Widget*, void*) {
        app->push_view_state(false, true);
        app->queue.clear();
        app->current_playlist_name.clear();
        app->current_loading_playlist_url.clear();
        app->view_items.clear();
        for (auto& it : Library::instance().favorites) {
            app->queue.push_back(it);
            app->view_items.push_back(it);
        }
        app->queue_view->current = -1;
        app->queue_view->reset_scroll();
        app->qpos = -1;
        app->pending_qpos = -1;
        app->user_pick_pending = false;
        app->is_loading = false;
        app->set_status(std::to_string(app->queue.size()) + " favoritos");
        app->queue_view->redraw();
        app->load_more_btn->hide();
    });

    downloads_btn = new FlatButton(10, 113, 180, 25, "Descargas Locales");
    downloads_btn->bg = C_BTN;
    downloads_btn->callback([](Fl_Widget*, void*) {
        app->push_view_state(false, false, true);
        app->queue.clear();
        app->current_playlist_name.clear();
        app->current_loading_playlist_url.clear();

        std::string dir;
        FILE* p = popen("xdg-user-dir DOWNLOAD", "r");
        if (p) {
            char buf[1024];
            if (fgets(buf, sizeof buf, p)) {
                std::string path(buf);
                while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
                    path.pop_back();
                if (!path.empty() && path != "/root" && path != "~/") dir = path + "/music";
            }
            pclose(p);
        }
        if (dir.empty()) {
            const char* home = getenv("HOME");
            dir = std::string(home ? home : "/tmp") + "/Downloads/music";
        }

        std::vector<std::string> files;
        DIR* d = opendir(dir.c_str());
        if (d) {
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                std::string name = ent->d_name;
                if (name.size() > 4 && name.substr(name.size() - 4) == ".mp3")
                    files.push_back(dir + "/" + name);
            }
            closedir(d);
        }
        std::sort(files.begin(), files.end());

        app->view_items.clear();
        for (auto& f : files) {
            Item it;
            std::string base = f.substr(f.rfind('/') + 1);
            it.title = base.substr(0, base.size() - 4);
            it.author = "Local";
            it.url = "file://" + f;
            it.kind = "video";
            app->queue.push_back(it);
            app->view_items.push_back(it);
        }

        app->queue_view->current = -1;
        app->queue_view->reset_scroll();
        app->qpos = -1;
        app->pending_qpos = -1;
        app->user_pick_pending = false;
        app->is_loading = false;
        app->set_status(std::to_string(app->queue.size()) + " canciones locales");
        app->queue_view->redraw();
        app->load_more_btn->hide();

        std::vector<std::string> snapshot = files;
        Fl::add_timeout(0.1, [](void* p) {
            auto* files = static_cast<std::vector<std::string>*>(p);
            std::thread([files]() {
                for (size_t i = 0; i < files->size(); i++) {
                    const std::string& path = (*files)[i];
                    double dur = probe_duration_real(path);
                    if (dur > 0) {
                        size_t idx = i;
                        double d = dur;
                        Fl::awake([](void* v) {
                            auto* pair = static_cast<std::pair<size_t,double>*>(v);
                            if (pair->first < app->queue.size() &&
                                app->queue[pair->first].url.rfind("file://", 0) == 0) {
                                app->queue[pair->first].duration = (int)(pair->second + 0.5);
                                app->queue_view->redraw();
                            }
                            delete pair;
                        }, new std::pair<size_t,double>(idx, d));
                    }
                }
                Fl::awake([](void*) {
                    app->set_status(std::to_string(app->queue.size()) + " canciones locales (duraciones cargadas)");
                }, nullptr);
                delete files;
            }).detach();
        }, new std::vector<std::string>(snapshot));
    });

    pl_title = new Label(10, 150, 120, 20);
    pl_title->size = 12; pl_title->fg_role = 1; pl_title->set("MIS PLAYLISTS");

    new_pl_btn = new FlatButton(140, 150, 50, 20, "+");
    new_pl_btn->use_global_bg = false;
    new_pl_btn->use_global_fg = false;
    new_pl_btn->bg = C_ACCENT; new_pl_btn->fg = C_BG;
    new_pl_btn->callback([](Fl_Widget*, void*) {
        Fl_Window* w = new Fl_Window(400, 150, "Nueva Playlist");
        w->color(C_BG);
        w->begin();
        Fl_Box* lbl = new Fl_Box(20, 15, 360, 25, "Nombre de la playlist:");
        lbl->labelcolor(C_TEXT);
        lbl->box(FL_FLAT_BOX);
        Fl_Input* in = new Fl_Input(20, 45, 360, 30);
        in->color(C_BTN); in->textcolor(C_TEXT); in->box(FL_FLAT_BOX);
        Fl_Button* ok = new Fl_Button(220, 100, 80, 28, "OK");
        ok->color(C_ACCENT); ok->labelcolor(C_BG); ok->box(FL_FLAT_BOX);
        Fl_Button* cancel = new Fl_Button(310, 100, 80, 28, "Cancelar");
        cancel->color(C_BTN); cancel->labelcolor(C_TEXT); cancel->box(FL_FLAT_BOX);
        w->end();
        w->set_modal();
        w->show();
        ok->callback([](Fl_Widget*, void* d) {
            Fl_Window* ww = (Fl_Window*)d;
            ww->hide();
        }, w);
        cancel->callback([](Fl_Widget*, void* d) {
            Fl_Window* ww = (Fl_Window*)d;
            ww->hide();
        }, w);
        while (w->shown()) Fl::wait();
        const char* name = in->value();
        if (name && *name) {
            Library::instance().create_playlist(name);
            app->refresh_library();
            app->set_status("Playlist '" + std::string(name) + "' creada");
        }
        delete w;
    });

    lib_browser = new PlaylistBrowser(10, 175, 180, H - 310);
    lib_browser->color(C_SIDE);
    lib_browser->textcolor(C_TEXT);
    lib_browser->selection_color(C_SEL);

    lib_browser->callback([](Fl_Widget* w, void*) {
        if (Fl::event_button() == FL_RIGHT_MOUSE) return;
        Fl_Hold_Browser* b = (Fl_Hold_Browser*)w;
        int line = b->value();
        if (line <= 0) return;
        std::string full_text = b->text(line);
        size_t pos = full_text.rfind(" (");
        std::string name = (pos != std::string::npos) ? full_text.substr(0, pos) : full_text;
        app->push_view_state(false, false, false, true);
        app->queue.clear();
        app->current_playlist_name = name;
        app->current_loading_playlist_url.clear();
        app->view_items.clear();
        for (auto& pl : Library::instance().playlists) {
            if (pl.first == name) {
                for (auto& it : pl.second) {
                    app->queue.push_back(it);
                    app->view_items.push_back(it);
                }
                break;
            }
        }
        app->queue_view->current = -1;
        app->queue_view->reset_scroll();
        app->qpos = -1;
        app->pending_qpos = -1;
        app->user_pick_pending = false;
        app->is_loading = false;
        app->set_status("Playlist: " + name + " (" + std::to_string(app->queue.size()) + ")");
        app->queue_view->redraw();
    });

    queue_view = new QueueView(200, 50, W - 200, H - 180);
    queue_view->items = &app->view_items;
    queue_view->on_play = [](int r) {
        app->play_queue(r);
        app->queue_view->redraw();
    };
    queue_view->on_play_item = [](const Item &it) {
        int idx = -1;
        for (int k = 0; k < (int)app->queue.size(); k++) {
            if (app->queue[k].url == it.url) { idx = k; break; }
        }
        if (idx < 0) {
            for (int k = 0; k < (int)app->queue.size(); k++) {
                if (app->queue[k].title == it.title && app->queue[k].author == it.author) {
                    idx = k; break;
                }
            }
        }
        if (idx < 0) {
            app->queue = app->view_items;
            app->current_playlist_name.clear();
            for (int k = 0; k < (int)app->queue.size(); k++) {
                if (app->queue[k].url == it.url) { idx = k; break; }
            }
            if (idx < 0) return;
        }
        app->play_queue(idx);
        app->queue_view->redraw();
    };

    load_more_btn = new FlatButton(200, H - 130, W - 200, 30, "Cargar más resultados");
    load_more_btn->bg = C_BTN;
    load_more_btn->callback([](Fl_Widget*, void*) {
        if (!app->current_loading_playlist_url.empty()) {
            app->playlist_start_index += 15;
            app->set_status("Cargando más canciones…", C_GREEN);
            app->load_more_btn->deactivate();
            app->is_loading = true;
            Fl::add_timeout(0.5, blink_status);
            int g = ++app->watch_gen;
            backend_expand_playlist(app->current_loading_playlist_url, app->playlist_start_index, [g](std::vector<Item> items) {
                if (g != app->watch_gen) return;
                app->is_loading = false;
                app->load_more_btn->activate();
                if (items.empty()) {
                    app->load_more_btn->label("No hay más canciones");
                } else {
                    for (auto& it : items) {
                        app->queue.push_back(it);
                        app->view_items.push_back(it);
                    }
                    app->set_status("Cargadas " + std::to_string(items.size()) + " canciones más");
                    app->queue_view->redraw();
                }
            });
            return;
        }
        app->search_start_index += 15;
        app->set_status("Cargando más resultados…", C_GREEN);
        app->load_more_btn->deactivate();
        app->is_loading = true;
        Fl::add_timeout(0.5, blink_status);
        backend_search(app->last_search_query, app->last_search_filter, app->search_start_index, [](Page pg) {
            for (auto& it : pg.items) app->view_items.push_back(it);
            app->queue_view->redraw();
            app->load_more_btn->activate();
            app->is_loading = false;
            if (pg.items.empty()) app->load_more_btn->label("No hay más resultados");
            else app->set_status("Cargados " + std::to_string(pg.items.size()) + " resultados más");
        });
    });
    load_more_btn->hide();

    controls = new Bar(0, H - 100, W, 100, 1);
    seek = new Seek(10, H - 95, W - 20, 20);
    seek->on_seek = [](double t) { if (app->cur_source >= 0) app->mplayer.seek(t); };

    int by = H - 65;

    prev_btn = new FlatButton(20, by, 40, 30, "");
    prev_btn->icon = I_PREV;
    prev_btn->callback([](Fl_Widget *, void *) { app->prev_track(); });

    play_btn = new FlatButton(65, by, 40, 30, "@>");
    play_btn->symbol = true;
    play_btn->callback([](Fl_Widget *, void *) {
        if (app->cur_source >= 0) app->mplayer.toggle_pause();
    });

    next_btn = new FlatButton(110, by, 40, 30, "");
    next_btn->icon = I_NEXT;
    next_btn->callback([](Fl_Widget *, void *) { app->next_track(false); });

    time_label = new Label(160, by, 120, 30);
    time_label->size = 14;
    time_label->font = FL_COURIER_BOLD;
    time_label->fg_role = 3;
    time_label->al = FL_ALIGN_LEFT | FL_ALIGN_INSIDE;

    shuffle_btn = new FlatButton(W - 260, by, 40, 30, "S");
    shuffle_btn->font_size = 14;
    shuffle_btn->callback([](Fl_Widget *, void *) { app->set_shuffle(!app->shuffle); });

    repeat_btn = new FlatButton(W - 215, by, 40, 30, "R");
    repeat_btn->font_size = 14;
    repeat_btn->callback([](Fl_Widget *, void *) {
        app->repeat_mode = (app->repeat_mode + 1) % 3;
        app->music_buttons();
        const char* labels[] = {
            "Repetir: No",
            "Repetir: Toda la Playlist",
            "Repetir: Canción en Bucle"
        };
        app->set_status(labels[app->repeat_mode]);
    });

    vol_icon = new Label(W - 170, by, 30, 30);
    vol_icon->size = 12;
    vol_icon->fg_role = 1;
    vol_icon->al = FL_ALIGN_CENTER;
    vol_icon->set("VOL");

    volume_bar = new VolumeBar(W - 105, by + 10, 100, 10);
    volume_bar->value_ = 80.0;
    volume_bar->on_change = [](double val) {
        app->mplayer.set_volume_absolute(val);
    };

    controls->end();

    status_bar = new Bar(0, H - 20, W, 20, 0);
    status = new Label(10, H - 20, W - 220, 20);
    status->font = FL_HELVETICA; status->size = 11; status->fg_role = 1;

    github_link = new LinkLabel(W - 140, H - 20, 130, 20);
    github_link->text = "github/IamJony";
    github_link->size = 11;
    github_link->font = FL_HELVETICA;
    github_link->fg_role = 2;
    github_link->on_click = []() {
        fl_open_uri("https://github.com/IamJony");
    };

    status_bar->end();

    win->end();
    win->resizable(win);
    win->callback([](Fl_Widget *, void *) { quit_app(); });
}

void App::refresh_library() {
    if (fav_btn) {
        char buf[64];
        snprintf(buf, sizeof buf, "Favoritos (%d)", (int)Library::instance().favorites.size());
        fav_btn->copy_label(buf);
        fav_btn->redraw();
    }
    if (lib_browser) {
        lib_browser->clear();
        for (auto& pl : Library::instance().playlists) {
            char buf[128];
            snprintf(buf, sizeof buf, "%s (%d)", pl.first.c_str(), (int)pl.second.size());
            lib_browser->add(buf);
        }
        lib_browser->redraw();
    }
}

void App::play_queue(int i) {
    if (i < 0 || i >= (int)queue.size()) return;
    qpos = i;
    play_gen++;
    bool is_local = queue[i].url.rfind("file://", 0) == 0;
    if (is_local) {
        pending_qpos = -1;
        user_pick_pending = false;
        is_loading = false;
    } else {
        pending_qpos = i;
        user_pick_pending = true;
    }
    queue_view->current = i;
    queue_view->redraw();
    open_item(queue[i]);
}

void App::open_item(const Item &it) {
    if (it.is_playlist) {
        set_status("Cargando playlist: " + clean(it.title) + "…", C_GREEN);
        is_loading = true;
        Fl::add_timeout(0.5, blink_status);
        current_loading_playlist_url = it.url;
        playlist_start_index = 0;
        int g = ++watch_gen;
        backend_expand_playlist(it.url, 0, [g](std::vector<Item> items) {
            if (g != app->watch_gen) return;
            app->is_loading = false;
            if (items.empty()) {
                app->set_status("La playlist está vacía o no se pudo cargar", C_RED);
                return;
            }
            app->queue = items;
            app->view_items = items;
            app->current_playlist_name.clear();
            app->qpos = -1;
            app->pending_qpos = -1;
            app->user_pick_pending = false;
            app->set_status("Playlist cargada: " + std::to_string(items.size()) + " canciones");
            app->queue_view->current = -1;
            app->queue_view->redraw();
            app->load_more_btn->label("Cargar más resultados");
            app->load_more_btn->show();
            app->layout();
        });
        return;
    }

    if (it.url.rfind("file://", 0) == 0) {
        cur = Details();
        cur.item = it;
        Source s;
        s.kind = S_DIRECT;
        s.url = it.url.substr(7);
        s.label = "Local";
        cur.sources.push_back(s);
        cur_source = 0;
        mplayer.stop();
        play_gen++;
        user_pick_pending = false;
        pending_qpos = -1;
        is_loading = false;
        mplayer.play(s.url, true);
        set_status("Reproduciendo local: " + clean(it.title), C_GREEN);
        showed_song_title = true;
        update_controls();
        return;
    }

    cur = Details();
    cur.item = it;
    cur_source = -1;
    is_loading = true;
    user_pick_pending = true;
    set_status("Cargando…", C_GREEN);

    Fl::add_timeout(15.0, [](void*) {
        if (app && app->user_pick_pending) app->user_pick_pending = false;
    });

    int g = ++watch_gen;
    backend_details(it.url, [g, it](Details d) {
        if (g != app->watch_gen) { app->user_pick_pending = false; return; }
        if (!d.error.empty()) {
            app->user_pick_pending = false;
            app->pending_qpos = -1;
            app->is_loading = false;
            app->set_status(d.error, C_RED);
            return;
        }
        if (!d.item.url.empty() && !it.url.empty() && d.item.url != it.url) {
            app->user_pick_pending = false;
            app->is_loading = false;
            app->set_status("Error: backend devolvio URL incorrecta", C_RED);
            return;
        }
        app->cur = d;
        app->play_source(0, 0);
    });
}

void App::play_source(int idx, double start) {
    if (idx < 0 || idx >= (int)cur.sources.size()) return;
    cur_source = idx;
    const Source &s = cur.sources[idx];

    mplayer.stop();
    play_gen++;
    user_pick_pending = false;
    pending_qpos = -1;
    is_loading = false;

    mplayer.play(s.url, true);
    last_play_at = (double)clock() / CLOCKS_PER_SEC;
    if (start > 0) {
        Fl::add_timeout(0.5, [](void* p) {
            double* d = (double*)p;
            app->mplayer.seek(*d);
            delete d;
        }, new double(start));
    }
    showed_song_title = false;
    set_status("Conectando...", C_GREEN);
    update_controls();
}

void App::update_controls() {
    if (cur_source < 0) return;

    double pos = mplayer.get_position();
    double len = mplayer.get_duration();

    bool is_local = false;
    if (cur_source >= 0 && cur_source < (int)cur.sources.size()) {
        const Source &s = cur.sources[cur_source];
        if (s.kind == S_DIRECT && !s.url.empty() && s.url[0] == '/') is_local = true;
    }

    if (is_local && cur.item.duration > 0) {
        len = cur.item.duration;
    } else if (len <= 0.5 && cur.item.duration > 0) {
        len = cur.item.duration;
    }

    if (is_local && cur.item.duration > 0 && pos >= cur.item.duration - 0.3) {
        if (mplayer.is_running() && !mplayer.is_paused()) {
            mplayer.stop();
            pos = cur.item.duration;
            set_status("Fin: " + clean_title(clean(cur.item.title)), C_GREEN);
            int gen = play_gen;
            Fl::add_timeout(0.3, [](void* p) {
                int ev_gen = (int)(intptr_t)p;
                if (!app) return;
                if (ev_gen != app->play_gen) return;
                if (app->cur_source < 0) return;
                app->user_pick_pending = false;
                app->pending_qpos = -1;
                app->next_track(true);
            }, (void*)(intptr_t)gen);
        }
    }

    double buffer = mplayer.get_buffer();

    if (pos < 0) pos = 0;
    if (len < 0) len = 0;
    if (len > 0 && pos > len) pos = len;

    const char *pl = mplayer.is_paused() ? "@>" : "@||";
    if (!play_btn->label() || strcmp(play_btn->label(), pl)) {
        play_btn->label(pl);
        play_btn->redraw();
    }

    char buf[64];
    snprintf(buf, sizeof buf, "%02d:%02d / %02d:%02d",
             (int)pos / 60, (int)pos % 60,
             (int)len / 60, (int)len % 60);
    time_label->set(buf);

    seek->max_ = len;
    seek->buffer_ = buffer;
    if (Fl::pushed() != seek) {
        seek->value_ = pos;
        seek->redraw();
    }

    if (mplayer.is_running() && !mplayer.is_paused()) {
        user_pick_pending = false;
        pending_qpos = -1;
    }

    if (mplayer.is_running() && !mplayer.is_paused() && pos > 0.5) {
        if (!showed_song_title) {
            std::string title;
            std::string artist;
            if (qpos >= 0 && qpos < (int)queue.size()) {
                title = clean_title(clean(queue[qpos].title));
                artist = clean(queue[qpos].author);
            }
            if (title.empty()) {
                title = clean_title(clean(cur.item.title));
                artist = clean(cur.item.author);
            }
            std::string msg = "Reproduciendo: " + title;
            if (!artist.empty()) msg += " - " + artist;
            set_status(msg, C_TEXT);
            showed_song_title = true;
        }
    } else if (pos == 0 && !mplayer.is_running()) {
        showed_song_title = false;
    }
}

void App::next_track(bool automatic) {
    if (automatic) {
        if (qpos < 0 || qpos >= (int)queue.size()) return;
        if (cur_source < 0) return;
        user_pick_pending = false;
        pending_qpos = -1;
    }

    if (automatic && repeat_mode == 2 && cur_source >= 0) {
        is_loading = false;
        if (cur_source < (int)cur.sources.size()) {
            mplayer.stop();
            play_gen++;
            mplayer.play(cur.sources[cur_source].url, true);
            showed_song_title = false;
            set_status("Repitiendo: " + clean_title(clean(cur.item.title)), C_GREEN);
            Fl::add_timeout(0.5, [](void*) {
                if (app->cur_source >= 0) app->mplayer.seek(0);
            }, nullptr);
            return;
        }
    }

    int next_idx = -1;
    int n = (int)queue.size();
    if (n <= 0) {
        set_status("Fin de la cola");
        mplayer.stop();
        return;
    }

    if (shuffle) {
        if (n == 1) {
            next_idx = (qpos == 0) ? -1 : 0;
        } else {
            std::vector<int> candidates;
            candidates.reserve(n - 1);
            for (int i = 0; i < n; i++) if (i != qpos) candidates.push_back(i);
            next_idx = candidates[rand() % candidates.size()];
        }
    } else {
        if (qpos + 1 < n) next_idx = qpos + 1;
        else if (repeat_mode == 1) next_idx = 0;
    }

    if (next_idx >= 0) play_queue(next_idx);
    else { set_status("Fin de la cola"); mplayer.stop(); }
}

void App::prev_track() {
    if (qpos > 0) play_queue(qpos - 1);
}

void App::set_shuffle(bool on) {
    shuffle = on;
    music_buttons();
    set_status(shuffle ? "Aleatorio activado" : "Aleatorio desactivado");
}

void App::music_buttons() {
    shuffle_btn->set_selected(shuffle);
    shuffle_btn->label(shuffle ? "S*" : "S");
    shuffle_btn->redraw();
    static const char* rl[] = {"R", "R*", "R1"};
    repeat_btn->label(rl[repeat_mode]);
    repeat_btn->set_selected(repeat_mode != 0);
    repeat_btn->redraw();
}

void App::stop_watch() {
    mplayer.stop();
}

void watchdog_tick(void *) { Fl::repeat_timeout(1.0, watchdog_tick); }

int main(int argc, char **argv) {
    unsetenv("LD_PRELOAD");
    setup_qjs_env();

    srand((unsigned)time(nullptr));
    Fl::lock();
    Fl::visual(FL_RGB);
    init_colors();
    apply_theme(load_saved_theme());
    Fl::scheme("none");
    backend_start("plugins", "js/sanson_host.js");
    Library::instance().load();
    app = new App();
    app->build();
    app->music_buttons();
    app->mplayer.init();
    app->mplayer.set_event_handler(on_mpv_event);
    app->win->show(argc, argv);
    app->ready = true;
    app->layout();
    app->refresh_library();

    Fl::add_timeout(0.5, update_timer);
    Fl::add_timeout(1.0, watchdog_tick);

    return Fl::run();
}

void App::push_view_state(bool is_search, bool is_fav, bool is_dl, bool is_pl) {
    ViewState v;
    v.view_items = view_items;
    v.queue = queue;
    v.qpos = qpos;
    v.current_playlist_name = current_playlist_name;
    v.current_loading_playlist_url = current_loading_playlist_url;
    v.playlist_start_index = playlist_start_index;
    v.search_start_index = search_start_index;
    v.last_search_query = last_search_query;
    v.last_search_filter = last_search_filter;
    v.load_more_label = load_more_btn->label() ? load_more_btn->label() : "";
    v.load_more_visible = load_more_btn->visible();
    v.is_search = is_search;
    v.is_fav = is_fav;
    v.is_dl = is_dl;
    v.is_pl = is_pl;
    view_stack.push_back(std::move(v));
}

void App::pop_view_state() {
    if (view_stack.empty()) {
        set_status("No hay vista anterior");
        return;
    }
    ViewState v = std::move(view_stack.back());
    view_stack.pop_back();
    view_items = std::move(v.view_items);
    queue = std::move(v.queue);
    qpos = v.qpos;
    current_playlist_name = v.current_playlist_name;
    current_loading_playlist_url = v.current_loading_playlist_url;
    playlist_start_index = v.playlist_start_index;
    search_start_index = v.search_start_index;
    last_search_query = v.last_search_query;
    last_search_filter = v.last_search_filter;
    if (load_more_btn) {
        load_more_btn->copy_label(v.load_more_label.c_str());
        load_more_btn->activate();
        if (v.load_more_visible) load_more_btn->show();
        else load_more_btn->hide();
        load_more_btn->redraw();
    }
    if (search) {
        search->value(v.last_search_query.c_str());
    }
    queue_view->current = -1;
    queue_view->reset_scroll();
    queue_view->redraw();
    win->redraw();
    set_status("Vista anterior restaurada (" + std::to_string(view_items.size()) + " items)");
}
