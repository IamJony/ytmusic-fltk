#ifndef SANSON_THEMES_H
#define SANSON_THEMES_H
#include <FL/Fl.H>
#include <string>

struct Theme {
    const char* name;
    Fl_Color bg, bar, side, panel, btn, btn_hi, sel, text, dim, accent, red, green, line;
};

extern const Theme themes[];
extern const int theme_count;
void apply_theme(int index);
int  load_saved_theme();
void save_theme(int index);

#endif
