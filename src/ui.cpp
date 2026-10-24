#include "ui.h"
#include <FL/Fl_JPEG_Image.H>
#include <FL/Fl_PNG_Image.H>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <list>
#include <map>
#include <unistd.h>

Fl_Color C_BG, C_BAR, C_SIDE, C_PANEL, C_BTN, C_BTN_HI, C_SEL,
         C_TEXT, C_DIM, C_ACCENT, C_RED, C_GREEN, C_LINE;
Fl_Color plat_color[P_COUNT];

void init_colors() {
    // Paleta oscura tipo Winamp
    C_BG = fl_rgb_color(25, 25, 25);          
    C_BAR = fl_rgb_color(15, 15, 15);         
    C_SIDE = fl_rgb_color(35, 35, 35);        
    C_PANEL = fl_rgb_color(30, 30, 30);       
    C_BTN = fl_rgb_color(50, 50, 50);         
    C_BTN_HI = fl_rgb_color(70, 70, 70);      
    C_SEL = fl_rgb_color(0, 120, 215);        
    C_TEXT = fl_rgb_color(200, 200, 200);     
    C_DIM = fl_rgb_color(120, 120, 120);      
    C_ACCENT = fl_rgb_color(0, 255, 0);       
    C_RED = fl_rgb_color(255, 50, 50);
    C_GREEN = fl_rgb_color(0, 255, 0);
    C_LINE = fl_rgb_color(10, 10, 10);
    plat_color[P_MUSIC] = fl_rgb_color(0, 255, 0);
}

std::string clean(const std::string &s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = s[i];
        if (c < 0x80) {
            if (c >= 32 || c == '\t') r += (c == '\t' ? ' ' : c);
            i++;
        } else if ((c >> 5) == 6) { // 2 bytes
            r += s.substr(i, 2); i += 2;
        } else if ((c >> 4) == 14) { // 3 bytes
            r += s.substr(i, 3); i += 3;
        } else if ((c >> 3) == 30) { // 4 bytes
            r += s.substr(i, 4); i += 4;
        } else {
            i++; 
        }
    }
    return r;
}

std::string fit(const std::string &s, int w) {
    if (fl_width(s.c_str()) <= w) return s;
    std::string r = s;
    while (!r.empty() && fl_width((r + "…").c_str()) > w) r.pop_back();
    return r + "…";
}
std::vector<std::string> wrap(const std::string &s, int w, int maxlines) {
    (void)maxlines;
    std::vector<std::string> out{fit(s, w)};
    return out;
}
double mono_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::function<void(const std::string &)> thumbs_changed;

Fl_RGB_Image *decode_raw(const std::string &data) {
    if (data.size() > 4 && (unsigned char)data[0] == 0xff && (unsigned char)data[1] == 0xd8)
        return new Fl_JPEG_Image(nullptr, (const unsigned char *)data.data());
    if (data.size() > 8 && (unsigned char)data[0] == 0x89 && data[1] == 'P')
        return new Fl_PNG_Image(nullptr, (const unsigned char *)data.data(), (int)data.size());
    return nullptr;
}

Fl_RGB_Image *get_thumb(const std::string &url, int w, int h, bool square) {
    (void)url; (void)w; (void)h; (void)square;
    return nullptr;
}

void draw_icon(Icon icon, int x, int y, int s, Fl_Color c) {
    fl_color(c);
    int cx = x + s / 2, cy = y + s / 2;
    switch (icon) {
    case I_PREV: fl_polygon(x + s - 2, y + 2, x + 5, cy, x + s - 2, y + s - 2);
                 fl_rectf(x + 2, y + 2, 3, s - 4); break;
    case I_NEXT: fl_polygon(x + 2, y + 2, x + s - 5, cy, x + 2, y + s - 2);
                 fl_rectf(x + s - 5, y + 2, 3, s - 4); break;
    case I_LOOP: 
        fl_line_style(FL_SOLID, 2);
        fl_arc(x + 2, y + 2, s - 4, s - 4, 40, 320); 
        fl_line_style(0); 
        break;
    case I_SHUFFLE:
        // Flechas cruzadas
        fl_line_style(FL_SOLID, 2);
        fl_line(x + 2, y + 4, x + s - 6, y + s - 4);
        fl_line(x + 2, y + s - 4, x + s - 6, y + 4);
        fl_polygon(x + s - 6, y + s - 4, x + s - 2, y + s - 2, x + s - 4, y + s - 6); // Flecha abajo der
        fl_polygon(x + s - 6, y + 4, x + s - 2, y + 2, x + s - 4, y + 6);             // Flecha arriba der
        fl_line_style(0);
        break;
    case I_REPEAT:
        // Dos flechas en círculo
        fl_line_style(FL_SOLID, 2);
        fl_arc(x + 4, y + 4, s - 8, s - 8, 45, 315);
        fl_line_style(0);
        // Flecha
        fl_polygon(x + s - 4, y + s / 2, x + s - 10, y + s / 2 - 6, x + s - 10, y + s / 2 + 6);
        break;
    case I_VOLUME:
        // Parlante
        fl_polygon(x + 2, y + s/3, x + 2, y + 2*s/3, x + s/2, y + 2*s/3, x + s/2, y + s/3);
        fl_polygon(x + s/2, y + 2*s/3, x + s/2, y + s/3, x + s - 2, y + s);
        fl_polygon(x + s/2, y + 2*s/3, x + s/2, y + s/3, x + s - 2, y + 1);
        // Ondas
        fl_arc(x + s/2, y + 2, s/2, s - 4, -60, 60);
        break;
    case I_YOUTUBE: {
        Fl_Color saved = fl_color();
        /* Rectángulo rojo con esquinas redondeadas y play blanco centrado.
         * Ratio ~1.4:1. Alto PAR para que los semicírculos encajen. */
        int bw = s;                       /* ancho total */
        int bh = (s * 7) / 10;
        if (bh % 2) bh++;                 /* forzar par */
        int bx = x;
        int by = y + (s - bh) / 2;        /* centrado vertical */
        int r  = bh / 2;                  /* radio = mitad del alto */

        /* Rojo YouTube */
        fl_color(fl_rgb_color(255, 0, 51));
        /* rectángulo central (entre las dos esquinas) */
        fl_rectf(bx + r, by, bw - 2*r, bh);
        /* esquina izquierda: semicírculo completo de 90 a 270 */
        fl_pie(bx, by, bh, bh, 90, 270);
        /* esquina derecha: semicírculo completo de 270 a 90 (a través de 360/0) */
        fl_pie(bx + bw - bh, by, bh, bh, 270, 90);

        /* Play blanco centrado */
        fl_color(FL_WHITE);
        int cx = bx + bw / 2;
        int cy = by + bh / 2;
        int tri = bh / 3;
        fl_polygon(cx - tri/2, cy - tri,
                   cx - tri/2, cy + tri,
                   cx + tri,   cy);

        fl_color(saved);
        break;
    }
    default: break;
    }
}

FlatButton::FlatButton(int x, int y, int w, int h, const char *l) : Fl_Button(x, y, w, h, l) {
    bg = C_BTN; hi = C_BTN_HI; fg = C_TEXT; sel_color = C_SEL;
    box(FL_FLAT_BOX); clear_visible_focus();
}
void FlatButton::draw() {
    Fl_Color bg_col = use_global_bg ? C_BTN : bg;
    Fl_Color hi_col = use_global_hi ? C_BTN_HI : hi;
    Fl_Color fg_col = use_global_fg ? C_TEXT : fg;
    Fl_Color bg_color;
    if (selected) {
        bg_color = C_SEL;
    } else if (value()) {
        bg_color = C_SEL;
    } else if (hover) {
        bg_color = hi_col;
    } else {
        bg_color = bg_col;
    }
    fl_rectf(x(), y(), w(), h(), bg_color);
    
    Fl_Color tc = active_r() ? fg_col : C_DIM;
    int lx = x() + 6 + align_left;
    
    if (icon != I_NONE) {
        int is = std::min(16, h() - 10);
        int ix = compact || !label() ? x() + (w() - is) / 2 : lx;
        draw_icon(icon, ix, y() + (h() - is) / 2, is, tc);
        lx = ix + is + 8;
        if (compact) return;
    }
    
    if (!label()) return;
    
    fl_font(FL_HELVETICA_BOLD, font_size);
    fl_color(tc);
    
    if (symbol) {
        fl_draw_symbol(label(), x() + w() / 2 - 7, y() + h() / 2 - 7, 14, 14, tc);
        return;
    }
    
    int lw = x() + w() - 6 - lx;
    fl_draw(fit(label(), lw).c_str(), lx, y(), lw, h(), FL_ALIGN_CENTER, nullptr, 0);
}
int FlatButton::handle(int e) {
    if (e == FL_ENTER || e == FL_LEAVE) { hover = e == FL_ENTER; redraw(); }
    return Fl_Button::handle(e);
}

void Label::set(const std::string &t) {
    if (t == text) return;
    text = t;
    if (parent() && visible_r()) parent()->damage(FL_DAMAGE_ALL, x(), y(), w(), h());
    else redraw();
}
void Label::draw() {
    fl_push_clip(x(), y(), w(), h());
    fl_font(font, size);
    Fl_Color c;
    switch (fg_role) {
        case 0:  c = C_TEXT;   break;
        case 1:  c = C_DIM;    break;
        case 2:  c = C_ACCENT; break;
        case 3:  c = C_GREEN;  break;
        case 4:  c = C_RED;    break;
        default: c = fg ? fg : C_TEXT; break;
    }
    fl_color(c);
    fl_draw(fit(text, w()).c_str(), x(), y(), w(), h(), al, nullptr, 0);
    fl_pop_clip();
}

DarkScroll::DarkScroll(int x, int y, int w, int h) : Fl_Scroll(x, y, w, h) {
    box(FL_FLAT_BOX); color(C_BG);
    type(Fl_Scroll::VERTICAL);
    scrollbar.color(C_BAR, C_BTN_HI);
    scrollbar.slider(FL_FLAT_BOX);
    scrollbar_size(12);
}




void VolumeBar::draw() {
    // Fondo de la barra
    fl_rectf(x(), y(), w(), h(), C_BAR);
    
    // Relleno del volumen
    int fw = (int)((w()) * (value_ / 100.0));
    fl_rectf(x(), y(), fw, h(), C_ACCENT);
    
    // Borde sutil (opcional)
    fl_color(C_BTN_HI);
    fl_rect(x(), y(), w(), h());
}

int VolumeBar::handle(int e) {
    if (e == FL_PUSH || e == FL_DRAG) {
        double v = (Fl::event_x() - x()) / (double)w() * 100.0;
        value_ = std::max(0.0, std::min(100.0, v));
        redraw();
        if (on_change) on_change(value_);
        return 1;
    }
    return Fl_Widget::handle(e);
}

void Seek::draw() {
    fl_rectf(x(), y(), w(), h(), C_BAR);
    int ty = y() + h() / 2 - 2;
    fl_rectf(x() + 4, ty, w() - 8, 4, C_BTN);
    if (max_ > 0) {
        // Dibujar buffer (parte cacheada)
        if (buffer_ > 0) {
            int bw = (int)((w() - 8) * std::min(1.0, buffer_ / max_));
            fl_rectf(x() + 4, ty, bw, 4, C_SEL); // color azul para el buffer
        }
        // Dibujar progreso (parte reproducida)
        int fw = (int)((w() - 8) * std::min(1.0, value_ / max_));
        fl_rectf(x() + 4, ty, fw, 4, C_ACCENT);
        fl_rectf(x() + 4 + fw - 3, y() + 1, 6, h() - 2, C_TEXT);
    }
}
int Seek::handle(int e) {
    if ((e == FL_PUSH || e == FL_DRAG) && max_ > 0) {
        double v = (Fl::event_x() - x() - 4) / (double)(w() - 8) * max_;
        value_ = std::max(0.0, std::min(max_, v));
        redraw(); return 1;
    }
    if (e == FL_RELEASE && max_ > 0) { if (on_seek) on_seek(value_); return 1; }
    return Fl_Widget::handle(e);
}

FlatChoice::FlatChoice(int x, int y, int w, int h) : Fl_Choice(x, y, w, h) {
    box(FL_FLAT_BOX); down_box(FL_FLAT_BOX);
    color(C_BTN); textcolor(C_TEXT); textfont(FL_HELVETICA_BOLD); textsize(12);
    labelcolor(C_TEXT); selection_color(C_SEL); clear_visible_focus();
}
void FlatChoice::draw() {
    fl_rectf(x(), y(), w(), h(), C_BTN);
    int aw = h() - 8;
    fl_color(C_ACCENT);
    int ax = x() + w() - aw - 2, ay = y() + h() / 2;
    fl_polygon(ax + 3, ay - 3, ax + aw - 5, ay - 3, ax + aw / 2 - 1, ay + 3);
    if (mvalue()) {
        fl_font(FL_HELVETICA_BOLD, 12); fl_color(active_r() ? C_TEXT : C_DIM);
        fl_draw(fit(mvalue()->label(), w() - aw - 14).c_str(), x() + 8, y(),
                w() - aw - 14, h(), FL_ALIGN_LEFT, nullptr, 0);
    }
}

void style_input(Fl_Input *in) {
    in->box(FL_FLAT_BOX);
    in->color(C_BTN);
    in->textcolor(C_TEXT); in->textsize(12);
    in->cursor_color(C_ACCENT); in->selection_color(C_SEL);
}
std::string menu_label(std::string l) { return l; }


Bar::Bar(int x, int y, int w, int h, Fl_Color c) : Fl_Group(x, y, w, h) { color(c); theme_id = -1; }
Bar::Bar(int x, int y, int w, int h, int tid) : Fl_Group(x, y, w, h) { theme_id = tid; }
Fl_Color Bar::resolve_color() const {
    switch (theme_id) {
        case 0: return C_BAR;
        case 1: return C_PANEL;
        case 2: return C_SIDE;
        case 3: return C_BG;
        default: return color();
    }
}


/* ===== LinkLabel (link clickeable tipo GitHub) ===== */
void LinkLabel::draw() {
    fl_push_clip(x(), y(), w(), h());
    fl_font(font, size);
    Fl_Color c;
    switch (fg_role) {
        case 0:  c = C_TEXT;   break;
        case 1:  c = C_DIM;    break;
        case 2:  c = C_ACCENT; break;
        case 3:  c = C_GREEN;  break;
        case 4:  c = C_RED;    break;
        default: c = C_ACCENT; break;
    }
    if (hover) {
        /* aclarar: mezclar con blanco 30% */
        unsigned r = (c >> 24) & 0xff, g = (c >> 16) & 0xff, b = (c >> 8) & 0xff;
        r = r + (255 - r) * 3 / 10;
        g = g + (255 - g) * 3 / 10;
        b = b + (255 - b) * 3 / 10;
        c = fl_rgb_color(r, g, b);
    }
    fl_color(c);
    fl_draw(text.c_str(), x(), y(), w(), h(), FL_ALIGN_RIGHT | FL_ALIGN_INSIDE, nullptr, 0);
    fl_pop_clip();
}
int LinkLabel::handle(int e) {
    switch (e) {
    case FL_ENTER: hover = true;  redraw(); return 1;
    case FL_LEAVE: hover = false; redraw(); return 1;
    case FL_PUSH:
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            if (on_click) on_click();
            return 1;
        }
        return 0;
    default:
        return Fl_Widget::handle(e);
    }
}
