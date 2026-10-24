#ifndef SANSON_UI_H
#define SANSON_UI_H
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_draw.H>
#include <functional>
#include <string>
#include <vector>
#include "backend.h"

extern Fl_Color C_BG, C_BAR, C_SIDE, C_PANEL, C_BTN, C_BTN_HI, C_SEL,
                C_TEXT, C_DIM, C_ACCENT, C_RED, C_GREEN, C_LINE;
extern Fl_Color plat_color[P_COUNT];
void init_colors();
std::string clean(const std::string &s);
std::string decode_unicode(const std::string &s);
std::string clean_title(const std::string &s);
std::string fit(const std::string &s, int w);
std::vector<std::string> wrap(const std::string &s, int w, int maxlines);
double mono_now();

Fl_RGB_Image *decode_raw(const std::string &data);
Fl_RGB_Image *get_thumb(const std::string &url, int w, int h, bool square);
extern std::function<void(const std::string &)> thumbs_changed;

enum Icon { I_NONE, I_HOME, I_NEW, I_FOLLOW, I_HISTORY, I_SETTINGS, I_EXIT,
            I_NEXT, I_PREV, I_LOOP, I_SHUFFLE, I_REPEAT, I_VOLUME, I_YOUTUBE };
void draw_icon(Icon icon, int x, int y, int size, Fl_Color c);

class FlatButton : public Fl_Button {
public:
    Fl_Color bg, hi, fg, sel_color;
    bool use_global_bg = true, use_global_hi = true, use_global_fg = true;
    bool selected = false, hover = false, symbol = false;
    int font_size = 12, align_left = 0;
    Icon icon = I_NONE;
    bool compact = false;
    FlatButton(int x, int y, int w, int h, const char *l = nullptr);
    void draw() override;
    int handle(int e) override;
    void set_selected(bool s) { if (s != selected) { selected = s; redraw(); } }
};

class Bar : public Fl_Group {
public:
    int theme_id = 0;
    Bar(int x, int y, int w, int h, Fl_Color c);
    Bar(int x, int y, int w, int h, int tid);
    Fl_Color resolve_color() const;
    void draw() override {
        if (damage() & ~FL_DAMAGE_CHILD) fl_rectf(x(), y(), w(), h(), resolve_color());
        draw_children();
    }
};

class Label : public Fl_Widget {
public:
    std::string text;
    int size = 13;
    Fl_Font font = FL_HELVETICA_BOLD;
    Fl_Color fg;
    int fg_role = -1; // -1=forzado, 0=TEXT, 1=DIM, 2=ACCENT, 3=GREEN, 4=RED
    Fl_Align al = FL_ALIGN_LEFT;
    Label(int x, int y, int w, int h) : Fl_Widget(x, y, w, h), fg(C_TEXT) {}
    void set(const std::string &t);
    void draw() override;
};

class LinkLabel : public Fl_Widget {
public:
    std::string text;
    int size = 11;
    Fl_Font font = FL_HELVETICA;
    int fg_role = 2;    /* 2=ACCENT por defecto */
    bool hover = false;
    std::function<void()> on_click;
    LinkLabel(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void set(const std::string &t) { if (t != text) { text = t; redraw(); } }
    void draw() override;
    int handle(int e) override;
};

class DarkScroll : public Fl_Scroll {
public:
    DarkScroll(int x, int y, int w, int h);
};


class Seek : public Fl_Widget {
public:
    double value_ = 0, max_ = 0, buffer_ = 0;
    std::function<void(double)> on_seek;
    Seek(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void draw() override;
    int handle(int e) override;
};


class VolumeBar : public Fl_Widget {
public:
    double value_ = 80.0; // Valor por defecto
    std::function<void(double)> on_change;
    VolumeBar(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    void draw() override;
    int handle(int e) override;
};
class FlatChoice : public Fl_Choice {
public:
    FlatChoice(int x, int y, int w, int h);
    void draw() override;
};

void style_input(Fl_Input *in);
std::string menu_label(std::string l);
#endif
