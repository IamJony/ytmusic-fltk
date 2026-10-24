#ifndef SANSON_VIEWS_H
#define SANSON_VIEWS_H
#include <FL/Fl_Window.H>
#include <deque>
#include "ui.h"

class VideoBox : public Fl_Window {
public:
    std::string text;
    std::string cover;
    std::function<void()> on_double, on_click;
    VideoBox(int x, int y, int w, int h);
    void draw() override;
    int handle(int e) override;
};

class LyricsView : public Fl_Widget {
    std::vector<std::pair<long long, std::string>> lines;
    std::vector<std::string> wrapped;
    std::vector<int> owner;
    int layout_w = -1, scroll_y = 0, cur = -1;
    double manual_until = 0;
    static const int HEAD = 26, LINE = 18;
    void relayout();
    int first_row(int line);
public:
    std::string plain, source, status;
    std::function<void(double)> on_seek;
    LyricsView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void set(const Lyrics &l);
    void clear_all(const std::string &st);
    void set_time(long long ms, double now);
    void draw() override;
    int handle(int e) override;
};

class QueueView : public Fl_Widget {
    int scroll_y = 0;
    static const int HEAD = 26, ROW = 34;
public:
    std::vector<Item> *items = nullptr;
    int current = -1, hover = -1;
    std::string mode;
    std::function<void(int)> on_play;
    std::function<void(const Item&)> on_play_item;  /* NUEVO: fuente de verdad */
    std::function<void(int, int)> on_menu;
    QueueView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void show_current();
    void reset_scroll() { scroll_y = 0; hover = -1; redraw(); }
    int row_at(int my);
    void draw() override;
    int handle(int e) override;
};

#endif
