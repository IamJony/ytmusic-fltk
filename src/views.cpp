#include "views.h"
#include "app.h"
#include "data.h"
#include <FL/fl_ask.H>
#include <FL/Fl_Menu_Button.H>
#include <cstdint>
#include <algorithm>
#include <cstring>

VideoBox::VideoBox(int x, int y, int w, int h) : Fl_Window(x, y, w, h) {
    color(FL_BLACK); box(FL_FLAT_BOX); end();
}
void VideoBox::draw() {
    fl_rectf(0, 0, w(), h(), FL_BLACK);
    if (!text.empty()) {
        fl_font(FL_HELVETICA_BOLD, 15); fl_color(C_DIM);
        fl_draw(text.c_str(), 10, 0, w() - 20, h(), FL_ALIGN_CENTER | FL_ALIGN_WRAP, nullptr, 0);
    }
}
int VideoBox::handle(int e) {
    if (e == FL_PUSH) {
        if (Fl::event_clicks() > 0) { Fl::event_clicks(0); if (on_double) on_double(); }
        else if (on_click) on_click();
        return 1;
    }
    return Fl_Window::handle(e);
}

void LyricsView::relayout() { layout_w = w() - 24; }
void LyricsView::set(const Lyrics &) { redraw(); }
void LyricsView::clear_all(const std::string &) { redraw(); }
void LyricsView::set_time(long long, double) { }
void LyricsView::draw() { fl_rectf(x(), y(), w(), h(), C_PANEL); }
int LyricsView::handle(int e) { return Fl_Widget::handle(e); }

void QueueView::show_current() { redraw(); }
int QueueView::row_at(int my) {
    int r = (my - (y() + HEAD) + scroll_y) / ROW;
    int ok = items && my > y() + HEAD && r >= 0 && r < (int)items->size() ? r : -1;
    return ok;
}
void QueueView::draw() {
    fl_push_clip(x(), y(), w(), h());
    fl_rectf(x(), y(), w(), h(), C_PANEL);
    fl_rectf(x(), y(), w(), HEAD, C_BAR);
    fl_font(FL_HELVETICA_BOLD, 12); fl_color(C_TEXT);
    int n = items ? (int)items->size() : 0;
    std::string head = "Cola · " + std::to_string(n) + (n == 1 ? " canción" : " canciones");
    fl_draw(fit(head, w() - 20).c_str(), x() + 10, y(), w() - 20, HEAD, FL_ALIGN_LEFT, nullptr, 0);
    fl_push_clip(x(), y() + HEAD, w(), h() - HEAD);
    int ry = y() + HEAD - scroll_y;
    for (int i = 0; i < n; i++, ry += ROW) {
        if (ry + ROW < y() + HEAD || ry > y() + h()) continue;
        if (i < 3) {
        }
        const Item &it = (*items)[i];
        
        // Determinar colores según el estado
        Fl_Color bg_color = (i == current) ? C_SEL : (i == hover) ? C_BTN : C_PANEL;
        Fl_Color text_color = (i == current) ? C_BG : C_TEXT;
        Fl_Color dim_color = (i == current) ? C_BG : C_DIM;
        
        if (i == current || i == hover)
            fl_rectf(x(), ry, w(), ROW, bg_color);
        
        fl_font(FL_HELVETICA_BOLD, 12); 
        fl_color(text_color);
        fl_draw(fit(clean_title(clean(it.title)), w() - 44).c_str(), x() + 34, ry + 15);
        
        fl_font(FL_HELVETICA, 10); 
        fl_color(dim_color);
        fl_draw(fit(clean(it.author), w() - 44).c_str(), x() + 34, ry + 28);
    }
    fl_pop_clip();
    if (!n) {
        fl_font(FL_HELVETICA, 12); fl_color(C_DIM);
        fl_draw("La cola está vacía.", x() + 10, y() + HEAD + 10, w() - 20, 60,
                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP, nullptr, 0);
    }
    fl_pop_clip();
}
int QueueView::handle(int e) {
    switch (e) {
    case FL_MOVE: { int r = row_at(Fl::event_y()); if (r != hover) { hover = r; redraw(); } return 1; }
    case FL_LEAVE: hover = -1; redraw(); return 1;
    case FL_MOUSEWHEEL: {
        int n = items ? (int)items->size() : 0;
        int max = std::max(0, n * ROW - (h() - HEAD) + 10);
        scroll_y = std::max(0, std::min(max, scroll_y + Fl::event_dy() * ROW * 2));
        redraw(); return 1;
    }
    case FL_PUSH: {
        int r = row_at(Fl::event_y());
        if (r >= 0) {
            if (Fl::event_button() == FL_LEFT_MOUSE) {
                const Item &clicked = (*items)[r];
                fprintf(stderr, "[QueueView] click r=%d title=%s\n",
                        r, clicked.title.c_str());
                if (on_play_item) {
                    on_play_item(clicked);
                } else if (on_play) {
                    /* fallback: buscar por URL para obtener indice real */
                    int real = r;
                    if (app) {
                        for (int k = 0; k < (int)app->queue.size(); k++) {
                            if (app->queue[k].url == clicked.url &&
                                app->queue[k].id  == clicked.id) {
                                real = k; break;
                            }
                        }
                    }
                    fprintf(stderr, "[QueueView] fallback real=%d\n", real);
                    on_play(real);
                }
            } else if (Fl::event_button() == FL_RIGHT_MOUSE) {
                Item& it = (*items)[r];
                bool is_fav = Library::instance().is_favorite(it.url);
                bool in_playlist = app && !app->current_playlist_name.empty();
                
                Fl_Menu_Item menu[] = {
                    {"Reproducir", 0, 0, 0, 0, 0, 0, 0, 0},
                    {"Añadir a la cola", 0, 0, 0, 0, 0, 0, 0, 0},
                    {is_fav ? "Quitar de Favoritos" : "Añadir a Favoritos", 0, 0, 0, 0, 0, 0, 0, 0},
                    {"Añadir a Playlist...", 0, 0, 0, 0, 0, 0, 0, 0},
                    {"Descargar MP3", 0, 0, 0, 0, 0, 0, 0, 0},
                    {"Quitar de esta Playlist", 0, 0, 0, in_playlist ? 0 : FL_MENU_INACTIVE, 0, 0, 0, 0},
                    {"Eliminar Playlist", 0, 0, 0, in_playlist ? 0 : FL_MENU_INACTIVE, 0, 0, 0, 0},
                    {0, 0, 0, 0, 0, 0, 0, 0, 0}
                };
                
                Fl_Menu_Button mb(0, 0, 0, 0); mb.color(C_PANEL); mb.textcolor(C_TEXT); mb.selection_color(C_SEL); const Fl_Menu_Item* m = menu->popup(Fl::event_x(), Fl::event_y(), 0, 0, &mb);
                if (m) {
                    int idx = (int)(m - menu);
                    if (idx == 0) {
                        if (on_play_item) on_play_item(it);
                        else if (on_play) on_play(r);
                    } else if (idx == 1) {
                        // Añadir a la cola
                        if (app) {
                            app->queue.push_back(it);
                            app->queue_view->redraw();
                            app->set_status("Añadido a la cola: " + clean_title(clean(it.title)));
                        }
                    } else if (idx == 2) {
                        if (is_fav) Library::instance().remove_favorite(it.url);
                        else Library::instance().add_favorite(it);
                        if (app) app->refresh_library();
                        redraw();
                    } else if (idx == 3) {
                        auto& lib = Library::instance();
                        if (lib.playlists.empty()) {
                            fl_message("No tienes playlists. Crea una con el botón '+'.");
                        } else {
                            Fl_Menu_Button* pl_menu = new Fl_Menu_Button(0, 0, 100, 100);
                            pl_menu->color(C_PANEL); pl_menu->textcolor(C_TEXT); pl_menu->selection_color(C_SEL);
                            for (auto& pl : lib.playlists) {
                                pl_menu->add(pl.first.c_str(), 0, nullptr);
                            }
                            const Fl_Menu_Item* chosen = pl_menu->popup();
                            if (chosen) {
                                std::string name = chosen->label();
                                lib.add_to_playlist(name, it);
                                if (app) app->refresh_library();
                                redraw();
                            }
                            delete pl_menu;
                        }
                    } else if (idx == 4) {
                        // Descargar MP3
                        app->is_downloading = true;
                        app->set_status("Descargando: " + clean_title(clean(it.title)), C_GREEN);
                        
                        std::string url = it.url;
                        std::string title = clean_title(clean(it.title));
                        
                        std::thread([url, title]() {
                            std::string dir;
                            FILE* p = popen("xdg-user-dir DOWNLOAD", "r");
                            if (p) {
                                char buf[1024];
                                if (fgets(buf, sizeof buf, p)) {
                                    std::string path(buf);
                                    while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
                                        path.pop_back();
                                    if (!path.empty() && path != "/root" && path != "~/") {
                                        dir = path + "/music";
                                    }
                                }
                                pclose(p);
                            }
                            if (dir.empty()) {
                                const char* home = getenv("HOME");
                                dir = std::string(home ? home : "/tmp") + "/Downloads/music";
                            }
                            mkdir(dir.c_str(), 0755);

                            std::string cmd = "yt-dlp -x --audio-format mp3 -o \"" + dir + "/%(title)s.%(ext)s\" \"" + url + "\" 2>/dev/null";
                            int ret = system(cmd.c_str());

                            Fl::awake([](void* p) {
                                std::string* msg = (std::string*)p;
                                app->is_downloading = false;
                                if (msg->rfind("Error", 0) == 0) {
                                    app->set_status(*msg, C_RED);
                                } else {
                                    app->set_status(*msg, C_GREEN);
                                }
                                app->status->show();
                                app->status->redraw();
                                delete msg;
                            }, new std::string(ret == 0 ? "Descarga completada: " + title : "Error al descargar: " + title));
                        }).detach();
                    } else if (idx == 5 && in_playlist) {
                        // Quitar de la playlist actual
                        Library::instance().remove_from_playlist(app->current_playlist_name, it.url);
                        if (app && r < (int)app->queue.size()) {
                            app->queue.erase(app->queue.begin() + r);
                            app->queue_view->current = -1;
                            app->queue_view->redraw();
                        }
                        app->set_status("Canción quitada de la playlist");
                        app->refresh_library();
                        redraw();
                    } else if (idx == 6 && in_playlist) {
                        // Eliminar la playlist completa
                        Library::instance().remove_playlist(app->current_playlist_name);
                        if (app) {
                            app->queue.clear();
                            app->current_playlist_name.clear();
                            app->queue_view->current = -1;
                            app->queue_view->redraw();
                        }
                        app->set_status("Playlist eliminada");
                        app->refresh_library();
                        redraw();
                    }
                }
            }
        }
        return 1;
    }
    }
    return Fl_Widget::handle(e);
}
