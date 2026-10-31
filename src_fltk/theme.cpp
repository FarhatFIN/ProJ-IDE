// language: C++20, module: theme — implementation
#include "theme.hpp"
#include "highlight.hpp"
#include "util.hpp"

#include <FL/Fl_Group.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Menu_.H>
#include <FL/Fl_Progress.H>

namespace theme {

namespace {

struct Pal {
    Fl_Color bg, bg2, fg, fg_dim, sel, ed_bg, ed_fg, red, green, act, side;
};

Pal g_pal{};

void set_pal(bool dark) {
    if (dark) {
        g_pal = {
            0x2b2f36ff, // bg     — charcoal
            0x363b44ff, // bg2    — panels
            0xe8e8e8ff, // fg
            0x9aa0a6ff, // fg_dim
            0x4a6fa5ff, // sel    — steel accent
            0x22262cff, // ed_bg
            0xdcdcdcff, // ed_fg
            0xe06c5fff, // red
            0x7dbd6aff, // green
            0x22262cff, // act — activity bar / status bar
            0x272b31ff, // side — sidebar
        };
    } else {
        g_pal = {
            0xf0f0f0ff,
            0xe2e4e8ff,
            0x202020ff,
            0x6a6f76ff,
            0x3567b6ff,
            0xffffffff,
            0x202020ff,
            0xc0261aff,
            0x1e7d32ff,
            0xdde0e4ff, // act
            0xe9ebeeff, // side
        };
    }
}

bool system_prefers_dark() {
    switch (util::os()) {
    case util::OS::Linux: {
        auto r = util::exec_capture({"gsettings", "get",
                                     "org.gnome.desktop.interface", "color-scheme"},
                                    std::nullopt, 2);
        if (r.exit_code == 0 && r.out.find("prefer-dark") != std::string::npos) return true;
        auto r2 = util::exec_capture({"gsettings", "get",
                                      "org.gnome.desktop.interface", "gtk-theme"},
                                     std::nullopt, 2);
        if (r2.exit_code == 0 && r2.out.find("-dark") != std::string::npos) return true;
        return false;
    }
    case util::OS::MacOS: {
        auto r = util::exec_capture({"defaults", "read", "-g", "AppleInterfaceStyle"},
                                    std::nullopt, 2);
        return r.exit_code == 0 && r.out.find("Dark") != std::string::npos;
    }
    case util::OS::Windows: {
        auto r = util::exec_capture(
            {"reg", "query",
             "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
             "/v", "AppsUseLightTheme"},
            std::nullopt, 2);
        // REG_DWORD 0x0 → dark apps
        return r.exit_code == 0 && r.out.find("0x0") != std::string::npos;
    }
    }
    return false;
}

// walk: push colors into every widget by kind
void recolor(Fl_Widget* w) {
    if (!w) return;
    if (auto* win = dynamic_cast<Fl_Window*>(w)) {
        win->color(bg());
        win->labelcolor(fg());
    } else if (auto* ed = dynamic_cast<Fl_Text_Display*>(w)) {
        ed->color(ed_bg());
        ed->labelcolor(fg_dim());
        ed->selection_color(sel());
        ed->cursor_color(ed_fg());
        ed->linenumber_bgcolor(ed_bg());
        ed->linenumber_fgcolor(fg_dim());
    } else if (auto* br = dynamic_cast<Fl_Browser*>(w)) {
        br->color(bg2());
        br->textcolor(fg());
        br->selection_color(sel());
    } else if (auto* in = dynamic_cast<Fl_Input*>(w)) {
        in->color(ed_bg());
        in->textcolor(ed_fg());
        in->cursor_color(ed_fg());
        in->labelcolor(fg());
    } else if (auto* pr = dynamic_cast<Fl_Progress*>(w)) {
        pr->color(bg2());
        pr->selection_color(sel());
        pr->labelcolor(fg());
    } else if (auto* b = dynamic_cast<Fl_Button*>(w)) {
        b->color(bg2());
        b->labelcolor(fg());
    } else if (dynamic_cast<Fl_Box*>(w)) {
        // keep per-widget labelcolor set by the owner; recolor only boxes
        // flagged by their current color being an old palette color — too
        // clever; instead: plain boxes take fg, callers override after apply.
        w->labelcolor(fg());
    } else {
        w->color(bg());
        w->labelcolor(fg());
    }
    if (auto* grp = dynamic_cast<Fl_Group*>(w)) {
        for (int i = 0; i < grp->children(); ++i) recolor(grp->child(i));
    }
}

} // namespace

void apply(ThemeMode m, Fl_Widget* root) {
    const bool dark = resolves_dark(m);
    set_pal(dark);
    hl::apply_theme(dark);

    Fl::set_color(FL_BACKGROUND_COLOR, bg());
    Fl::set_color(FL_BACKGROUND2_COLOR, bg2());
    Fl::set_color(FL_FOREGROUND_COLOR, fg());
    Fl::set_color(FL_INACTIVE_COLOR, bg2());
    Fl::set_color(FL_SELECTION_COLOR, sel());

    recolor(root);
    if (root) root->redraw();
}

bool resolves_dark(ThemeMode m) {
    if (m == ThemeMode::Dark) return true;
    if (m == ThemeMode::Light) return false;
    return system_prefers_dark();
}

Fl_Color bg()      { return g_pal.bg; }
Fl_Color bg2()     { return g_pal.bg2; }
Fl_Color fg()      { return g_pal.fg; }
Fl_Color fg_dim()  { return g_pal.fg_dim; }
Fl_Color sel()     { return g_pal.sel; }
Fl_Color ed_bg()   { return g_pal.ed_bg; }
Fl_Color ed_fg()   { return g_pal.ed_fg; }
Fl_Color red()     { return g_pal.red; }
Fl_Color green()   { return g_pal.green; }
Fl_Color act()     { return g_pal.act; }
Fl_Color side()    { return g_pal.side; }

} // namespace theme
