// language: C++20, file: main.cpp, target: Linux/Windows/macOS, FLTK 1.3.11
// ProJ IDE — desktop adaptation of the web UI (activity bar, palette, stdin,
// status bar, breadcrumbs, problems/output/input panel). Local toolchain only.
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/fl_ask.H>

#include "util.hpp"
#include "lang.hpp"
#include "build.hpp"
#include "store.hpp"
#include "highlight.hpp"
#include "theme.hpp"
#include "install.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------- CodeEditor

// Fl_Text_Editor with a themed line-number gutter. Debian ships FLTK with the
// old ABI where linenumber_bgcolor/fgcolor setters are no-ops, so after the
// base draw() we repaint the gutter with our own palette.
struct CodeEditor : Fl_Text_Editor {
    Fl_Color ln_bg = 0x22262cff;
    Fl_Color ln_fg = 0x9aa0a6ff;
    using Fl_Text_Editor::Fl_Text_Editor;
    void draw() override {
        Fl_Text_Editor::draw();
        paint_linenums();
    }
    void paint_linenums() {
        if (mLineNumWidth <= 0 || !visible_r()) return;
        const int hscroll_h = mHScrollBar->visible() ? mHScrollBar->h() : 0;
        const int xoff = Fl::box_dx(box());
        const int yoff = Fl::box_dy(box()) +
                         ((scrollbar_align() & FL_ALIGN_TOP) ? hscroll_h : 0);
        fl_push_clip(x() + xoff, y() + yoff, mLineNumWidth,
                     h() - Fl::box_dw(box()) - hscroll_h);
        fl_color(active_r() ? ln_bg : fl_inactive(ln_bg));
        fl_rectf(x() + xoff, y(), mLineNumWidth, h());
        fl_font(FL_COURIER, 11);
        fl_color(active_r() ? ln_fg : fl_inactive(ln_fg));
        int Y = y() + yoff;
        int line = get_absolute_top_line_number();
        const int lineHeight = mMaxsize;
        for (int visLine = 0; visLine < mNVisibleLines; ++visLine) {
            const int lineStart = mLineStarts[visLine];
            if (lineStart != -1 &&
                (lineStart == 0 || buffer()->char_at(lineStart - 1) == '\n')) {
                char s[16];
                snprintf(s, sizeof(s), "%d", line);
                fl_draw(s, x() + xoff + 3, Y + 3, mLineNumWidth - 6, lineHeight,
                        FL_ALIGN_RIGHT, nullptr, 0);
                ++line;
            } else if (visLine == 0) {
                ++line;
            }
            Y += lineHeight;
        }
        fl_pop_clip();
    }
};

// ---------------------------------------------------------------- state

struct Session {
    Store store;
    int cur_proj = -1;
    std::string cur_file;

    std::map<std::string, Fl_Text_Buffer*> bufs;
    std::map<std::string, Fl_Text_Buffer*> styles;
    std::map<std::string, Lang> langs;
    std::map<std::string, Fl_Button*> tabs;
    std::map<std::string, bool> dirty;

    struct Row { int proj = -1; int file = -1; };
    struct ErrRef { std::string path; int line = 0; int col = 0; };
    std::vector<Row> rows;                       // parallel to explorer browser
    std::vector<ErrRef> err_refs;

    std::vector<BuildError> diags;               // last build diagnostics
    int err_count = 0, warn_count = 0;

    // widgets — chrome
    Fl_Double_Window* win = nullptr;
    Fl_Group* titlebar = nullptr;
    Fl_Group* activity = nullptr;
    Fl_Button* act_explorer = nullptr;
    Fl_Button* act_run = nullptr;
    Fl_Button* act_settings = nullptr;
    Fl_Button* btn_palette = nullptr;
    Fl_Button* btn_start = nullptr;
    Fl_Group* sidebar = nullptr;
    Fl_Group* view_explorer = nullptr;
    Fl_Group* view_run = nullptr;
    Fl_Group* view_settings = nullptr;
    Fl_Browser* browser = nullptr;
    Fl_Button* btn_new_proj = nullptr;
    Fl_Button* btn_add_files = nullptr;
    Fl_Button* btn_badge = nullptr;
    Fl_Browser* diag_list = nullptr;
    Fl_Group* tabstrip = nullptr;
    Fl_Button* btn_close_tab = nullptr;
    Fl_Box* breadcrumb = nullptr;
    Fl_Group* editor_area = nullptr;
    CodeEditor* editor = nullptr;
    Fl_Group* empty_state = nullptr;
    Fl_Button* btn_empty_add = nullptr;
    Fl_Tabs* bottom_tabs = nullptr;
    Fl_Browser* prob_list = nullptr;
    Fl_Text_Display* out_disp = nullptr;
    Fl_Text_Buffer* out_buf = nullptr;
    Fl_Text_Editor* stdin_ed = nullptr;
    Fl_Text_Buffer* stdin_buf = nullptr;
    Fl_Button* status_err = nullptr;
    Fl_Box* status_right = nullptr;
    Fl_Round_Button* rb_dark = nullptr;
    Fl_Round_Button* rb_light = nullptr;
    Fl_Round_Button* rb_system = nullptr;

    // palette window
    Fl_Double_Window* pal_win = nullptr;
    Fl_Input* pal_input = nullptr;
    Fl_Browser* pal_list = nullptr;

    int last_line = -1, last_col = -1;
    bool restyle_pending = false;
} g;

static constexpr int W = 1200, H = 720;
static constexpr int ACT_X = 0, ACT_W = 44;
static constexpr int SB_X = 44, SB_W = 264;
static constexpr int MAIN_X = 308;
static constexpr int TITLE_H = 32, STATUS_H = 24, BOTTOM_H = 200;
static constexpr int ERRBLOCK_H = 176;

// ---------------------------------------------------------------- helpers

static fs::path bin_path(const fs::path& src) {
    fs::path out = src;
#ifdef _WIN32
    out.replace_extension(".exe");
#else
    out.replace_extension("");
#endif
    return out;
}

static void update_title() {
    std::string t = "ProJ IDE";
    if (g.cur_proj >= 0 && g.cur_proj < static_cast<int>(g.store.projects.size()))
        t += " — " + g.store.projects[g.cur_proj].name;
    if (!g.cur_file.empty())
        t += " : " + fs::path(g.cur_file).filename().string();
    g.win->copy_label(t.c_str());
}

static void relayout_tabs() {
    if (!g.tabstrip) return;
    int x = 0;
    for (auto& [path, btn] : g.tabs) {
        btn->position(g.tabstrip->x() + x, g.tabstrip->y() + 1);
        x += btn->w() + 2;
    }
    g.tabstrip->redraw();
}

static void refresh_tab_label(const std::string& path) {
    auto it = g.tabs.find(path);
    if (it == g.tabs.end()) return;
    std::string label = fs::path(path).filename().string();
    if (g.dirty[path]) label += " *";
    it->second->copy_label(label.c_str());
    g.tabstrip->redraw();
}

static void refresh_tab_styles() {
    for (auto& [path, btn] : g.tabs) {
        const bool active = (path == g.cur_file);
        btn->box(active ? FL_THIN_UP_BOX : FL_FLAT_BOX);
        btn->color(active ? theme::sel() : theme::bg2());
        btn->labelcolor(active ? FL_WHITE : theme::fg());
    }
    g.tabstrip->redraw();
}

static void refresh_activity() {
    const auto set = [](Fl_Button* b, bool active) {
        b->box(active ? FL_THIN_UP_BOX : FL_FLAT_BOX);
        b->color(active ? theme::sel() : theme::act());
        b->labelcolor(active ? FL_WHITE : theme::fg_dim());
    };
    set(g.act_explorer, g.view_explorer->visible());
    set(g.act_run, g.view_run->visible());
    set(g.act_settings, g.view_settings->visible());
    g.activity->redraw();
}

static void show_view(int which) { // 0 explorer, 1 run, 2 settings
    g.view_explorer->show(); g.view_run->hide(); g.view_settings->hide();
    if (which == 1) { g.view_explorer->hide(); g.view_run->show(); }
    if (which == 2) { g.view_explorer->hide(); g.view_run->hide(); g.view_settings->show(); }
    refresh_activity();
}

static void update_badges() {
    const std::string s = "Errors<" + std::to_string(g.err_count) + ">";
    g.btn_badge->copy_label(s.c_str());
    g.btn_badge->color(g.err_count > 0 ? theme::red() : theme::bg2());
    g.btn_badge->labelcolor(g.err_count > 0 ? FL_WHITE : theme::fg_dim());
    g.btn_badge->redraw();
    const std::string st = "✖ " + std::to_string(g.err_count) + "   ⚠ " +
                           std::to_string(g.warn_count);
    g.status_err->copy_label(st.c_str());
    g.status_err->labelcolor(g.err_count > 0 ? theme::red() : theme::fg_dim());
    g.status_err->redraw();
}

static void update_status_lang() {
    std::string right = "Стр " + std::to_string(std::max(1, g.last_line)) +
                        ", стлб " + std::to_string(std::max(1, g.last_col)) + "   UTF-8   ";
    if (!g.cur_file.empty()) right += lang_name(lang_by_ext(fs::path(g.cur_file)));
    else right += "—";
    g.status_right->copy_label(right.c_str());
    g.status_right->redraw();
}

static void update_breadcrumb() {
    std::string b;
    if (g.cur_proj >= 0 && g.cur_proj < static_cast<int>(g.store.projects.size()))
        b = g.store.projects[g.cur_proj].name;
    if (!g.cur_file.empty()) {
        if (!b.empty()) b += "  ›  ";
        // show path relative to the project's first file dir when possible
        b += fs::path(g.cur_file).filename().string();
    }
    g.breadcrumb->copy_label(b.c_str());
    g.breadcrumb->redraw();
}

static void center_overlay() {
    if (!g.empty_state || g.empty_state->children() == 0) return;
    Fl_Widget* c = g.empty_state->child(0);
    c->position(g.empty_state->x() + (g.empty_state->w() - c->w()) / 2,
                g.empty_state->y() + (g.empty_state->h() - c->h()) / 2);
}

static void update_empty_state() {
    const bool empty = g.cur_file.empty();
    if (empty) {
        g.editor->hide();
        g.empty_state->show();
        center_overlay();
    } else {
        g.empty_state->hide();
        g.editor->show();
    }
    g.editor_area->redraw();
}

static void select_tab(const std::string& path);
void extern_open(const std::string& path);
static void close_file(const std::string& path);
static void palette_fill_enter(const std::string& row_text);
static void add_files_flow(int project_idx);
static void start_cb(Fl_Widget*, void*);
static void run_cb(Fl_Widget*, void*);
static void save_cb(Fl_Widget*, void*);

static void goto_line(const std::string& path, int line, int col) {
    auto it = g.bufs.find(path);
    if (it == g.bufs.end()) return;
    int pos = it->second->skip_lines(0, std::max(0, line - 1));
    if (col > 1) pos = std::min(it->second->length(), pos + col - 1);
    g.editor->buffer(it->second);
    g.editor->insert_position(pos);
    g.editor->show_insert_position();
    g.editor->take_focus();
}

// ---------------------------------------------------------------- restyle

static void restyle_now() {
    g.restyle_pending = false;
    if (g.cur_file.empty()) return;
    auto b = g.bufs.find(g.cur_file);
    auto s = g.styles.find(g.cur_file);
    auto l = g.langs.find(g.cur_file);
    if (b == g.bufs.end() || s == g.styles.end() || l == g.langs.end()) return;
    hl::restyle(b->second, s->second, l->second);
    g.editor->redraw();
}

static void restyle_timer_cb(void*) { restyle_now(); }

static void cursor_timer_cb(void*) {
    if (!g.cur_file.empty()) {
        auto b = g.bufs.find(g.cur_file);
        if (b != g.bufs.end()) {
            const int pos = g.editor->insert_position();
            const int line = b->second->count_lines(0, pos) + 1;
            const int col = pos - b->second->line_start(pos) + 1;
            if (line != g.last_line || col != g.last_col) {
                g.last_line = line; g.last_col = col;
                update_status_lang();
            }
        }
    }
    Fl::add_timeout(0.15, cursor_timer_cb, nullptr);
}

static void buf_modify_cb(int pos, int ins, int del, int restyled,
                          const char* deleted, void*) {
    (void)pos; (void)restyled; (void)deleted;
    if (ins == 0 && del == 0) return;
    if (!g.cur_file.empty()) {
        g.dirty[g.cur_file] = true;
        refresh_tab_label(g.cur_file);
    }
    if (!g.restyle_pending) {
        g.restyle_pending = true;
        Fl::add_timeout(0.30, restyle_timer_cb, nullptr);
    }
}

// ---------------------------------------------------------------- diagnostics

static std::vector<BuildError> diags_src; // diagnostics of the last build

static void fill_errors() {
    g.diags.clear();
    g.err_refs.clear();
    g.err_count = g.warn_count = 0;
    g.diag_list->clear();
    g.prob_list->clear();
    for (const auto& e : diags_src) {
        g.diags.push_back(e);
        (e.is_error ? g.err_count : g.warn_count)++;
        const std::string icon = e.is_error ? "✖" : "⚠";
        std::string line = icon + "  ";
        if (e.line > 0) line += std::to_string(e.line) + ": ";
        line += e.msg;
        g.diag_list->add(line.c_str());
        g.prob_list->add((line + (e.col > 0 ? "  [" + std::to_string(e.line) + ", " +
                                    std::to_string(e.col) + "]" : "")).c_str());
        // gcc logs paths relative to the build dir — resolve against the built file
        fs::path fp(e.file);
        if (fp.empty()) fp = g.cur_file;
        else if (fp.is_relative()) fp = fs::path(g.cur_file).parent_path() / fp;
        g.err_refs.push_back({fp.string(), e.line, e.col > 0 ? e.col : 1});
    }
    update_badges();
}

static void store_diags(const BuildResult& res) {
    diags_src = res.errors;
    fill_errors();
}

static void diag_click_common(int line) {
    if (line <= 0 || line > static_cast<int>(g.err_refs.size())) return;
    const auto& [path, ln, col] = g.err_refs[line - 1];
    if (path.empty()) return;
    if (g.bufs.find(path) == g.bufs.end()) extern_open(path);
    else select_tab(path);
    if (ln > 0) goto_line(path, ln, col);
}

// ---------------------------------------------------------------- file ops

static void select_tab(const std::string& path) {
    auto b = g.bufs.find(path);
    auto s = g.styles.find(path);
    auto l = g.langs.find(path);
    if (b == g.bufs.end() || s == g.styles.end() || l == g.langs.end()) return;
    g.cur_file = path;
    g.editor->buffer(b->second);
    int n = 0;
    const auto* table = hl::style_table(n);
    g.editor->highlight_data(s->second, table, n, 'Z', nullptr, nullptr);
    g.editor->insert_position(0);
    g.editor->show_insert_position();
    g.editor->take_focus();
    refresh_tab_styles();
    update_title();
    update_breadcrumb();
    update_status_lang();
    update_empty_state();
    diags_src.clear();
    fill_errors();
}

void extern_open(const std::string& path) {
    if (g.bufs.count(path)) { select_tab(path); return; }

    auto buf = new Fl_Text_Buffer();
    if (buf->loadfile(path.c_str()) != 0) {
        fl_alert("Не удалось открыть файл:\n%s", path.c_str());
        delete buf;
        return;
    }
    const Lang lang = lang_by_ext(fs::path(path));
    auto style = new Fl_Text_Buffer(buf->length());
    hl::restyle(buf, style, lang);
    g.bufs[path] = buf;
    g.styles[path] = style;
    g.langs[path] = lang;
    g.dirty[path] = false;
    buf->add_modify_callback(buf_modify_cb, nullptr);

    auto btn = new Fl_Button(0, 0, 150, 22, fs::path(path).filename().string().c_str());
    btn->box(FL_FLAT_BOX);
    btn->tooltip("ЛКМ — открыть · ПКМ — закрыть");
    btn->callback([](Fl_Widget* w, void* data) {
        (void)w;
        auto p = static_cast<std::string*>(data);
        if (Fl::event_button() == FL_RIGHT_MOUSE) {
            close_file(*p);
        } else {
            select_tab(*p);
        }
    }, new std::string(path));
    g.tabstrip->add(btn);
    g.tabs[path] = btn;
    relayout_tabs();

    g.cur_file = path;
    g.editor->buffer(buf);
    int n = 0;
    const auto* table = hl::style_table(n);
    g.editor->highlight_data(style, table, n, 'Z', nullptr, nullptr);
    g.editor->insert_position(0);
    refresh_tab_label(path);
    refresh_tab_styles();
    update_title();
    update_breadcrumb();
    update_status_lang();
    update_empty_state();
    diags_src.clear();
    fill_errors();
}

static void close_file(const std::string& path) {
    auto b = g.bufs.find(path);
    if (b == g.bufs.end()) return;
    if (g.dirty[path]) {
        int r = fl_choice("Сохранить изменения в «%s»?",
                          "Не сохранять", "Сохранить", "Отмена",
                          fs::path(path).filename().string().c_str());
        if (r == 2) return;
        if (r == 1) b->second->savefile(path.c_str());
    }
    std::string next;
    bool found = false;
    for (auto& [p, btn] : g.tabs) {
        if (p == path) { found = true; continue; }
        if (found && next.empty()) next = p;
    }
    if (next.empty() && g.tabs.size() > 1) {
        for (auto& [p, btn] : g.tabs) if (p != path) { next = p; break; }
    }

    if (auto it = g.tabs.find(path); it != g.tabs.end()) {
        auto* data = static_cast<std::string*>(it->second->user_data());
        g.tabstrip->remove(it->second);
        delete it->second;
        delete data;
        g.tabs.erase(it);
        relayout_tabs();
    }
    b->second->remove_modify_callback(buf_modify_cb, nullptr);
    delete b->second;
    g.bufs.erase(b);
    if (auto s = g.styles.find(path); s != g.styles.end()) { delete s->second; g.styles.erase(s); }
    g.dirty.erase(path);
    g.langs.erase(path);

    if (g.cur_file == path) {
        g.cur_file.clear();
        if (!next.empty()) select_tab(next);
    }
    update_empty_state();
    update_title();
    update_breadcrumb();
}

// ---------------------------------------------------------------- browser

static void rebuild_browser() {
    g.browser->clear();
    g.rows.clear();
    for (int i = 0; i < static_cast<int>(g.store.projects.size()); ++i) {
        const Project& p = g.store.projects[i];
        std::string line = std::to_string(i + 1) + ".  " + p.name;
        g.browser->add(line.c_str());
        g.rows.push_back({i, -1});
        if (i == g.cur_proj) {
            for (const auto& f : p.files) {
                const bool is_last = (f == p.last_open);
                g.browser->add(("      " + f.filename().string() +
                                (is_last ? "   ●" : "")).c_str());
                g.rows.push_back({i, static_cast<int>(&f - p.files.data())});
            }
        }
    }
    int sel_line = 0;
    for (size_t r = 0; r < g.rows.size(); ++r)
        if (g.rows[r].proj == g.cur_proj && g.rows[r].file == -1) { sel_line = int(r) + 1; break; }
    if (sel_line) g.browser->value(sel_line);
}

static void browser_menu(int line);

static void browser_cb(Fl_Widget*, void*) {
    const int line = g.browser->value();
    if (Fl::event_button() == FL_RIGHT_MOUSE) { browser_menu(line); return; }
    if (line <= 0 || line > static_cast<int>(g.rows.size())) return;
    const Session::Row& row = g.rows[line - 1];
    if (row.file >= 0) {
        const Project& p = g.store.projects[row.proj];
        const fs::path f = p.files[row.file];
        g.cur_proj = row.proj;
        if (f.string() != g.cur_file) extern_open(f.string());
        else select_tab(f.string());
    } else {
        g.cur_proj = row.proj;
        const Project& p = g.store.projects[row.proj];
        if (!p.files.empty()) {
            fs::path target = p.last_open;
            bool ok = false;
            for (const auto& f : p.files) if (f == target) { ok = true; break; }
            if (!ok) target = p.files.front();
            if (target.string() != g.cur_file) extern_open(target.string());
            else select_tab(target.string());
        }
    }
    rebuild_browser();
    g.store.set_last(g.cur_proj, fs::path(g.cur_file));
    g.store.save();
    update_title();
    update_breadcrumb();
}

static void browser_menu(int line) {
    if (line <= 0 || line > static_cast<int>(g.rows.size())) return;
    const Session::Row& row = g.rows[line - 1];
    Fl_Menu_Item items[] = {
        {"Новый файл",        0, nullptr, (void*)1, FL_MENU_VALUE, 0, 0, 0, 0},
        {"Добавить файлы…",   0, nullptr, (void*)2, 0,             0, 0, 0, 0},
        {"Удалить проект",    0, nullptr, (void*)3, 0,             0, 0, 0, 0},
        {nullptr,             0, nullptr, nullptr,  0,             0, 0, 0, 0},
    };
    if (row.file >= 0) {
        items[0].label("Удалить из проекта");
        items[1].flags = 0; items[1].label(nullptr);
        items[2].label(nullptr);
    }
    const Fl_Menu_Item* m = items->popup(Fl::event_x(), Fl::event_y());
    if (!m) return;
    const intptr_t id = (intptr_t)m->user_data();

    if (row.file >= 0 && id == 1) {
        // remove file reference
        const Project& p = g.store.projects[row.proj];
        const fs::path f = p.files[row.file];
        if (fl_choice("Убрать «%s» из проекта?\n(файл на диске не удаляется)",
                      "Отмена", "Убрать", nullptr,
                      f.filename().string().c_str()) != 1) return;
        g.store.remove_file(row.proj, f);
        close_file(f.string());
        g.store.save();
        rebuild_browser();
        return;
    }
    if (row.file < 0) {
        if (id == 3) { // delete project
            const Project& p = g.store.projects[row.proj];
            if (fl_choice("Удалить проект «%s» из списка?\n(файлы на диске останутся)",
                          "Отмена", "Удалить", nullptr, p.name.c_str()) != 1) return;
            for (const auto& f : p.files) close_file(f.string());
            g.store.remove_project(row.proj);
            g.cur_proj = -1;
            g.store.save();
            rebuild_browser();
            update_title();
            return;
        }
        if (id == 2) { add_files_flow(row.proj); return; }
        if (id == 1) { // new file inside project
            const Project& p = g.store.projects[row.proj];
            std::string def = p.files.empty()
                ? (util::home_dir() / "main.cpp").string()
                : (p.files.front().parent_path() / "main.cpp").string();
            const char* r = fl_input("Имя файла (полный путь):", def.c_str());
            if (!r || !*r) return;
            fs::path nf(r);
            std::error_code ec;
            if (!fs::exists(nf, ec)) {
                std::ofstream f(nf, std::ios::binary);
                if (!f) { fl_alert("Не удалось создать файл:\n%s", nf.string().c_str()); return; }
            }
            g.store.add_files(row.proj, {nf});
            g.cur_proj = row.proj;
            g.store.save();
            rebuild_browser();
            extern_open(nf.string());
        }
    }
}

// ---------------------------------------------------------------- files add

static std::vector<fs::path> pick_files() {
    Fl_Native_File_Chooser chooser;
    chooser.title("Выберите файлы");
    chooser.type(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
    chooser.filter("*\tВсе файлы (*)");
    if (chooser.show() != 0) return {};
    std::vector<fs::path> out;
    for (int i = 0; i < chooser.count(); ++i)
        out.push_back(fs::path(chooser.filename(i)));
    return out;
}

static void add_files_flow(int project_idx) {
    std::vector<fs::path> files = pick_files();
    if (files.empty()) return;
    const bool have = project_idx >= 0 &&
                      project_idx < static_cast<int>(g.store.projects.size());
    if (have) {
        g.store.add_files(project_idx, files);
        g.cur_proj = project_idx;
    } else {
        std::string def = "Проект " + std::to_string(g.store.projects.size() + 1);
        const char* name = fl_input("Название вашего проекта:", def.c_str());
        if (!name || !*name) return;
        g.store.add_project(name, files);
        g.cur_proj = static_cast<int>(g.store.projects.size()) - 1;
    }
    g.store.save();
    rebuild_browser();
    if (!files.empty()) extern_open(files.front().string());
    update_title();
    update_empty_state();
}

static void add_cb(Fl_Widget*, void*) {
    const bool have = g.cur_proj >= 0 &&
                      g.cur_proj < static_cast<int>(g.store.projects.size());
    int mode = 1;
    if (have)
        mode = fl_choice("Добавить файлы в текущий проект или создать новый?",
                         "Отмена", "Новый проект", "В текущий");
    add_files_flow(mode == 0 ? g.cur_proj : -1);
}

static void new_proj_cb(Fl_Widget*, void*) { add_files_flow(-1); }

// ---------------------------------------------------------------- save

static void do_save(bool silent) {
    if (g.cur_file.empty()) {
        if (!silent) fl_alert("Нет открытого файла.");
        return;
    }
    auto b = g.bufs.find(g.cur_file);
    if (b == g.bufs.end()) return;
    if (b->second->savefile(g.cur_file.c_str()) != 0) {
        if (!silent) fl_alert("Не удалось сохранить:\n%s", g.cur_file.c_str());
        return;
    }
    g.dirty[g.cur_file] = false;
    refresh_tab_label(g.cur_file);
}

static void save_cb(Fl_Widget*, void*) { do_save(false); }

// ---------------------------------------------------------------- palette

static void palette_fill(const std::string& filter) {
    g.pal_list->clear();
    const std::string f = util::to_lower(filter);
    for (int i = 0; i < static_cast<int>(g.store.projects.size()); ++i) {
        const Project& p = g.store.projects[i];
        for (const auto& file : p.files) {
            const std::string name = file.filename().string();
            if (!f.empty() &&
                util::to_lower(name).find(f) == std::string::npos &&
                util::to_lower(p.name).find(f) == std::string::npos) continue;
            g.pal_list->add(("  " + name + "    — " + p.name).c_str());
        }
    }
}

static void palette_open() {
    if (!g.pal_win) {
        g.pal_win = new Fl_Double_Window(g.win->x() + 300, g.win->y() + 80, 520, 420,
                                         "Найти файл");
        g.pal_win->set_modal();
        auto* in = new Fl_Input(10, 10, 500, 28);
        in->callback([](Fl_Widget* w, void*) {
            if (Fl::event_key() == FL_Enter) { // FL_WHEN_ENTER_KEY: open top row
                g.pal_list->value(g.pal_list->value() ? g.pal_list->value() : 1);
                const char* t = g.pal_list->text(g.pal_list->value());
                if (t) palette_fill_enter(std::string(t));
                return;
            }
            palette_fill(static_cast<Fl_Input*>(w)->value());
        });
        in->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY | FL_WHEN_NOT_CHANGED);
        auto* list = new Fl_Browser(10, 46, 500, 364);
        list->type(FL_HOLD_BROWSER);
        list->textfont(FL_COURIER);
        list->callback([](Fl_Widget* w, void*) {
            auto* br = static_cast<Fl_Browser*>(w);
            const char* t = br->text(br->value());
            if (t) palette_fill_enter(std::string(t)); // single click opens
        });
        g.pal_input = in;
        g.pal_list = list;
        g.pal_win->end();
    }
    g.pal_input->value("");
    palette_fill("");
    g.pal_win->show();
    g.pal_input->take_focus();
}

// opens the file for the currently highlighted palette row
static void palette_fill_enter(const std::string& row_text) {
    // row format: "  name    — project"
    // row: "  <file>    — <project>" — slice AFTER the full 3-byte em-dash
    const size_t dash = row_text.find("—");
    if (dash == std::string::npos) return;
    std::string fname = util::trim(row_text.substr(0, dash));
    std::string pname = util::trim(row_text.substr(dash + 3));
    for (int i = 0; i < static_cast<int>(g.store.projects.size()); ++i) {
        if (g.store.projects[i].name != pname) continue;
        for (const auto& f : g.store.projects[i].files) {
            if (f.filename().string() == fname) {
                g.cur_proj = i;
                g.store.save();
                rebuild_browser();
                extern_open(f.string());
                if (g.pal_win) g.pal_win->hide();
                return;
            }
        }
    }
}

static void palette_cb(Fl_Widget*, void*) { palette_open(); }

// ---------------------------------------------------------------- build flow

static void start_cb(Fl_Widget*, void*);

static bool install_flow(const std::string& tool) {
    Fl_Window dlg(380, 130, "Установка компилятора");
    dlg.set_modal();
    Fl_Box status(16, 16, 348, 40, "Подготовка...");
    status.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    Fl_Progress bar(16, 76, 348, 22);
    bar.minimum(0); bar.maximum(1); bar.value(0);
    dlg.end();

    std::atomic<int> percent{-1};
    std::atomic<bool> cancel{false};
    std::mutex mtx;
    std::string phase = "Проверка сети...";
    std::string detail;
    install::Outcome outcome = install::Outcome::Failed;
    bool done = false;

    std::thread worker([&] {
        outcome = install::install(tool, percent, cancel, phase, detail);
        std::lock_guard<std::mutex> lk(mtx);
        done = true;
    });

    dlg.show();
    double pulse = 0.0;
    while (!done) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            std::string txt = phase;
            const int p = percent.load();
            if (p >= 0) txt += " " + std::to_string(p) + "%";
            status.copy_label(txt.c_str());
            if (p >= 0) bar.value(p / 100.0);
            else { pulse = std::fmod(pulse + 0.02, 1.0); bar.value(pulse); }
        }
        Fl::wait(0.08);
    }
    worker.join();
    dlg.hide();

    switch (outcome) {
    case install::Outcome::NetworkError:
        fl_alert("Ошибка сети.\nПроверьте подключение и попробуйте снова.");
        return false;
    case install::Outcome::Failed: {
        std::string tail = detail.size() > 800 ? detail.substr(detail.size() - 800) : detail;
        fl_alert("Не удалось установить «%s».\n\n%s", tool.c_str(),
                 tail.empty() ? "(подробностей нет)" : tail.c_str());
        return false;
    }
    case install::Outcome::Ok:
        break;
    }

    std::string ver;
    if (!install::tool_present(tool, ver)) {
        fl_alert("«%s» всё ещё не найден в PATH.\nВозможно, требуется перезапуск ProJ IDE.",
                 tool.c_str());
        return false;
    }
    fl_message("Установлено: %s\n%s", tool.c_str(), ver.c_str());
    return true;
}

static void show_panel(int which) { // 0 problems, 1 output, 2 input
    if (which == 0) g.bottom_tabs->value(g.bottom_tabs->child(0));
    if (which == 1) g.bottom_tabs->value(g.bottom_tabs->child(1));
    if (which == 2) g.bottom_tabs->value(g.bottom_tabs->child(2));
    g.bottom_tabs->redraw();
}

static void start_cb(Fl_Widget*, void*) {
    if (g.cur_file.empty()) {
        fl_alert("Нет открытого файла.\nДобавьте файлы через «+ Добавить файлы».");
        return;
    }
    const fs::path src(g.cur_file);
    const Lang lang = lang_by_ext(src);
    if (!lang_compilable(lang)) {
        fl_alert("Тип файла «%s» не компилируется.", lang_name(lang));
        return;
    }
    do_save(true);

    const std::string tool = tool_for(lang);
    std::string ver;
    if (!install::tool_present(tool, ver)) {
        std::string q = std::string("У вас не нашлось \"") + tool +
                        "\".\nМы можем скачать его из доверенных источников.";
        if (fl_choice(q.c_str(), "Выйти", "Разрешаю", nullptr) != 1) return;
        if (!install_flow(tool)) return;
    }

    const std::string stdin_text = g.stdin_buf->text();
    BuildResult res = build_file(src, stdin_text);
    g.out_buf->text(res.log.c_str());
    store_diags(res);

    if (!res.ok) {
        show_panel(0);
        std::string first_num, first_msg, first_path;
        int first_line = 0, first_col = 1;
        for (const auto& e : res.errors) {
            if (e.is_error) {
                first_num = std::to_string(e.line);
                first_msg = e.msg;
                first_line = e.line;
                first_col = e.col > 0 ? e.col : 1;
                fs::path fp(e.file.empty() ? g.cur_file : e.file);
                if (fp.is_relative()) fp = src.parent_path() / fp;
                first_path = fp.string();
                break;
            }
        }
        const int go = fl_choice("Ошибка компиляции — строка %s\n%s", "Закрыть",
                                 "Перейти", nullptr, first_num.c_str(), first_msg.c_str());
        if (go == 1 && first_line > 0) goto_line(first_path, first_line, first_col);
    } else {
        show_panel(1);
        if (res.log.empty()) g.out_buf->text("Сборка успешна.");
        else g.out_buf->append("\n— Сборка успешна.\n");
        // python: run right here with the stdin panel content (web-parity)
        if (lang == Lang::Python) {
            auto r = util::exec_capture({std::string("python3"), "-X", "utf8",
                                         src.filename().string()},
                                        src.parent_path(), 30, stdin_text);
            std::string out = r.out;
            if (r.timed_out) out += "\n[превышено время 30 с — процесс остановлен]";
            g.out_buf->append(("$ python3 " + src.filename().string() + "\n" +
                               (out.empty() ? "(вывода нет)" : out) + "\n").c_str());
        }
    }
}

static void run_cb(Fl_Widget*, void*) {
    if (g.cur_file.empty()) return;
    const fs::path src(g.cur_file);
    const Lang lang = lang_by_ext(src);
    std::string cmd;
    switch (lang) {
    case Lang::Python:
        cmd = "python3 '" + util::replace_all(src.string(), "'", "'\\''") + "'";
        break;
    case Lang::Cpp: case Lang::C: case Lang::Asm: {
        fs::path bin = bin_path(src);
        std::error_code ec;
        if (!fs::exists(bin, ec)) {
            fl_alert("Сначала соберите файл кнопкой «Старт».");
            return;
        }
        cmd = "'" + util::replace_all(bin.string(), "'", "'\\''") + "'";
        break;
    }
    default:
        fl_alert("Этот файл нельзя запустить.");
        return;
    }
    util::run_detached_terminal(cmd, src.parent_path());
}

// ---------------------------------------------------------------- errors click

static void diag_list_cb(Fl_Widget* w, void*) {
    auto* br = static_cast<Fl_Browser*>(w);
    diag_click_common(br->value());
}

static void prob_list_cb(Fl_Widget* w, void*) {
    auto* br = static_cast<Fl_Browser*>(w);
    diag_click_common(br->value());
}

static void badge_cb(Fl_Widget*, void*) {
    show_view(0);
    show_panel(0);
}

static void status_err_cb(Fl_Widget*, void*) { show_panel(0); }

// ---------------------------------------------------------------- theme

static void apply_theme_ui(ThemeMode mode) {
    theme::apply(mode, g.win);
    g.editor->ln_bg = theme::ed_bg();
    g.editor->ln_fg = theme::fg_dim();
    // chrome strips
    const auto setg = [](Fl_Group* grp, Fl_Color c) {
        if (grp) { grp->color(c); grp->redraw(); }
    };
    setg(g.titlebar, theme::act());
    setg(g.activity, theme::act());
    setg(g.sidebar, theme::side());
    setg(g.tabstrip, theme::bg());
    g.breadcrumb->color(theme::bg());
    g.status_err->color(theme::act());
    g.status_right->color(theme::act());
    if (g.pal_win) { g.pal_win->color(theme::bg()); g.pal_win->redraw(); }
    refresh_tab_styles();
    refresh_activity();
    update_badges();
    g.win->redraw();
}

static void theme_radio_cb(Fl_Widget* /*w*/, void* data) {
    const intptr_t mode = (intptr_t)data; // 0 dark, 1 light, 2 system
    apply_theme_ui(mode == 0 ? ThemeMode::Dark :
                   mode == 1 ? ThemeMode::Light : ThemeMode::System);
}

// ---------------------------------------------------------------- activity

static void act_cb(Fl_Widget* /*w*/, void* data) {
    const intptr_t v = (intptr_t)data;
    show_view(static_cast<int>(v));
}

// ---------------------------------------------------------------- overlay

struct Overlay : Fl_Group {
    using Fl_Group::Fl_Group;
    void resize(int X, int Y, int W2, int H2) override {
        Fl_Group::resize(X, Y, W2, H2);
        if (children()) {
            const int bw = 340, bh = 150;
            child(0)->resize(X + (W2 - bw) / 2, Y + (H2 - bh) / 2, bw, bh);
        }
    }
};

// ---------------------------------------------------------------- quit

static void quit_cb(Fl_Widget*, void*) {
    bool any_dirty = false;
    for (auto& [path, d] : g.dirty) if (d) { any_dirty = true; break; }
    if (any_dirty) {
        const int r = fl_choice("Есть несохранённые изменения. Сохранить всё перед выходом?",
                                "Выйти без сохранения", "Сохранить и выйти", "Отмена");
        if (r == 2) return;
        if (r == 1) for (auto& [path, b] : g.bufs) b->savefile(path.c_str());
    }
    g.store.set_last(g.cur_proj, fs::path(g.cur_file));
    g.store.save();
    g.win->hide();
}

// ---------------------------------------------------------------- build UI

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    Fl::scheme("gtk+");
    g.store.load();

    auto* win = new Fl_Double_Window(W, H, "ProJ IDE");
    g.win = win;
    win->size_range(900, 560);
    win->callback(quit_cb);

    // ---- title bar ----
    {
        auto* tb = new Fl_Group(0, 0, W, TITLE_H);
        tb->box(FL_FLAT_BOX);
        g.titlebar = tb;
        auto* logo = new Fl_Box(8, 4, 80, 24, "ProJ IDE");
        logo->labelfont(FL_BOLD + FL_COURIER);
        logo->labelsize(13);
        auto* pal = new Fl_Button(140, 5, 340, 22, "Поиск файлов…   Ctrl+P");
        pal->box(FL_THIN_UP_BOX);
        pal->labelsize(11);
        pal->shortcut(FL_COMMAND + 'p');
        pal->callback(palette_cb);
        g.btn_palette = pal;
        auto* save = new Fl_Button(W - 236, 5, 108, 22, "Сохранить");
        save->shortcut(FL_COMMAND + 's');
        save->tooltip("Ctrl+S");
        save->callback(save_cb);
        auto* start = new Fl_Button(W - 120, 5, 112, 22, "▶  Старт");
        start->shortcut(FL_COMMAND + 'b');
        start->labelfont(FL_BOLD);
        start->tooltip("Скомпилировать (Ctrl+B) · Ctrl+Enter тоже");
        start->callback(start_cb);
        g.btn_start = start;
        tb->end();
    }

    // ---- activity bar ----
    {
        auto* act = new Fl_Group(ACT_X, TITLE_H, ACT_W, H - TITLE_H - STATUS_H);
        act->box(FL_FLAT_BOX);
        g.activity = act;
        const auto mk = [&](int y, const char* label, intptr_t id, const char* tip) {
            auto* b = new Fl_Button(ACT_X, TITLE_H + y, ACT_W, 40, label);
            b->box(FL_FLAT_BOX);
            b->labelsize(16);
            b->tooltip(tip);
            b->callback(act_cb, (void*)id);
            return b;
        };
        g.act_explorer = mk(6, "☰", 0, "Проводник");
        g.act_run = mk(50, "▶", 1, "Запуск");
        g.act_settings = mk(94, "⚙", 2, "Настройки");
        act->end();
    }

    // ---- sidebar ----
    {
        auto* sb = new Fl_Group(SB_X, TITLE_H, SB_W, H - TITLE_H - STATUS_H);
        sb->box(FL_FLAT_BOX);
        g.sidebar = sb;

        // explorer view
        {
            const int vh = H - TITLE_H - STATUS_H - ERRBLOCK_H;
            auto* v = new Fl_Group(SB_X, TITLE_H, SB_W, vh);
            v->box(FL_FLAT_BOX);
            g.view_explorer = v;
            auto* hdr = new Fl_Box(SB_X + 8, TITLE_H + 4, 120, 20, "ПРОЕКТЫ");
            hdr->labelfont(FL_BOLD);
            hdr->labelsize(10);
            hdr->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            hdr->labelcolor(theme::fg_dim());
            auto* np = new Fl_Button(SB_X + SB_W - 146, TITLE_H + 3, 82, 20, "Новый проект");
            np->box(FL_THIN_UP_BOX);
            np->labelsize(9);
            np->tooltip("Новый проект (файлы + имя)");
            np->callback(new_proj_cb);
            auto* af = new Fl_Button(SB_X + SB_W - 60, TITLE_H + 3, 54, 20, "Файлы+");
            af->box(FL_THIN_UP_BOX);
            af->labelsize(9);
            af->tooltip("+ Добавить файлы");
            af->callback(add_cb);
            auto* br = new Fl_Browser(SB_X, TITLE_H + 26, SB_W, vh - 26);
            br->when(FL_WHEN_CHANGED);
            br->callback(browser_cb);
            br->textsize(12);
            br->tooltip("Проекты. Клик — раскрыть и открыть последний файл. ПКМ — действия.");
            g.browser = br;
            v->end();

            // errors block (bottom of sidebar, anchored bottom-right via add-after)
            auto* eb = new Fl_Group(SB_X, TITLE_H + vh, SB_W, ERRBLOCK_H);
            eb->box(FL_FLAT_BOX);
            auto* badge = new Fl_Button(SB_X + 8, TITLE_H + vh + 6, 96, 20, "Errors<0>");
            badge->box(FL_THIN_UP_BOX);
            badge->labelfont(FL_BOLD);
            badge->labelsize(11);
            badge->callback(badge_cb);
            badge->tooltip("Показать проблемы");
            g.btn_badge = badge;
            auto* dl = new Fl_Browser(SB_X, TITLE_H + vh + 30, SB_W, ERRBLOCK_H - 30);
            dl->textsize(11);
            dl->callback(diag_list_cb);
            dl->when(FL_WHEN_CHANGED);
            dl->tooltip("Ошибки и предупреждения. Клик — перейти к строке.");
            g.diag_list = dl;
            eb->end();
        }

        // run view
        {
            const int vh = H - TITLE_H - STATUS_H;
            auto* v = new Fl_Group(SB_X, TITLE_H, SB_W, vh);
            v->box(FL_FLAT_BOX);
            g.view_run = v;
            v->hide();
            auto* hdr = new Fl_Box(SB_X + 12, TITLE_H + 8, SB_W - 24, 20, "ЗАПУСК");
            hdr->labelfont(FL_BOLD);
            hdr->labelsize(10);
            hdr->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            hdr->labelcolor(theme::fg_dim());
            auto* st = new Fl_Button(SB_X + 12, TITLE_H + 34, SB_W - 24, 28, "▶  Старт");
            st->labelfont(FL_BOLD);
            st->callback(start_cb);
            st->shortcut(FL_COMMAND + 'b');
            auto* trm = new Fl_Button(SB_X + 12, TITLE_H + 68, SB_W - 24, 24, "Запуск в терминале");
            trm->callback(run_cb);
            trm->tooltip("Бинарник/скрипт в отдельном терминале (интерактивный stdin)");
            auto* info = new Fl_Box(SB_X + 12, TITLE_H + 100, SB_W - 24, 150,
                "C++    g++ -std=c++20 -Wall -Wextra\n"
                "C      gcc -std=c17 -Wall -Wextra\n"
                "Python python3 + ввод из панели\n"
                "NASM   nasm + gcc (elf64/win64/macho64)\n\n"
                "Сборка локальная. Python после\n"
                "сборки запускается прямо в панели\n"
                "«Вывод» с данными из «Ввод».\n"
                "Бинарники: кнопка «Запуск»\n"
                "открывает терминал.");
            info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_TOP | FL_ALIGN_WRAP);
            info->labelsize(11);
            info->labelcolor(theme::fg_dim());
            auto* keys = new Fl_Box(SB_X + 12, TITLE_H + vh - 70, SB_W - 24, 60,
                "Ctrl+B / Ctrl+Enter — старт\nCtrl+S — сохранить\nCtrl+P — найти файл");
            keys->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_TOP | FL_ALIGN_WRAP);
            keys->labelsize(11);
            keys->labelcolor(theme::fg_dim());
            v->end();
        }

        // settings view
        {
            const int vh = H - TITLE_H - STATUS_H;
            auto* v = new Fl_Group(SB_X, TITLE_H, SB_W, vh);
            v->box(FL_FLAT_BOX);
            g.view_settings = v;
            v->hide();
            auto* hdr = new Fl_Box(SB_X + 12, TITLE_H + 8, SB_W - 24, 20, "ТЕМА");
            hdr->labelfont(FL_BOLD);
            hdr->labelsize(10);
            hdr->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            hdr->labelcolor(theme::fg_dim());
            auto* d = new Fl_Round_Button(SB_X + 12, TITLE_H + 36, SB_W - 24, 22, "Тёмная");
            d->callback(theme_radio_cb, (void*)0);
            g.rb_dark = d;
            auto* l = new Fl_Round_Button(SB_X + 12, TITLE_H + 62, SB_W - 24, 22, "Светлая");
            l->callback(theme_radio_cb, (void*)1);
            g.rb_light = l;
            auto* s = new Fl_Round_Button(SB_X + 12, TITLE_H + 88, SB_W - 24, 22, "Системная");
            s->callback(theme_radio_cb, (void*)2);
            s->set();
            g.rb_system = s;
            v->end();
        }

        sb->end();
    }

    // ---- main area (resizable) ----
    {
        auto* main = new Fl_Group(MAIN_X, TITLE_H, W - MAIN_X, H - TITLE_H - STATUS_H);
        win->resizable(main);

        // tabs row
        {
            auto* row = new Fl_Group(MAIN_X, TITLE_H, W - MAIN_X, 30);
            row->box(FL_FLAT_BOX);
            auto* close = new Fl_Button(W - 32, TITLE_H + 3, 26, 24, "x");
            close->box(FL_FLAT_BOX);
            close->labelfont(FL_BOLD);
            close->tooltip("Закрыть текущий файл");
            close->callback([](Fl_Widget*, void*) {
                if (!g.cur_file.empty()) close_file(g.cur_file);
            });
            auto* strip = new Fl_Group(MAIN_X + 2, TITLE_H + 2, W - MAIN_X - 40, 26);
            strip->box(FL_FLAT_BOX);
            strip->end();
            row->end();
            g.tabstrip = strip;
            g.btn_close_tab = close;
        }

        // breadcrumb
        {
            auto* bc = new Fl_Box(MAIN_X + 12, TITLE_H + 31, W - MAIN_X - 24, 18, "");
            bc->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            bc->labelsize(11);
            bc->labelcolor(theme::fg_dim());
            g.breadcrumb = bc;
        }

        // editor + overlay
        {
            const int ed_h = H - TITLE_H - STATUS_H - 30 - 20 - BOTTOM_H;
            auto* area = new Fl_Group(MAIN_X, TITLE_H + 50, W - MAIN_X, ed_h + 20);
            main->resizable(area);
            g.editor_area = area;

            auto* ed = new CodeEditor(MAIN_X, TITLE_H + 51, W - MAIN_X, ed_h);
            area->resizable(ed);
            ed->textfont(FL_COURIER);
            ed->textsize(14);
            ed->wrap_mode(Fl_Text_Display::WRAP_NONE, 0);
            ed->linenumber_width(52);
            ed->buffer(new Fl_Text_Buffer());
            g.editor = ed;

            auto* overlay = new Overlay(MAIN_X, TITLE_H + 51, W - MAIN_X, ed_h);
            overlay->box(FL_FLAT_BOX);
            auto* card = new Fl_Group(0, 0, 340, 150);
            card->box(FL_FLAT_BOX);
            auto* big_logo = new Fl_Box(0, 0, 340, 60, "ProJ");
            big_logo->labelfont(FL_BOLD + FL_COURIER);
            big_logo->labelsize(42);
            big_logo->align(FL_ALIGN_CENTER);
            auto* hint = new Fl_Box(0, 64, 340, 24,
                                    "Ctrl+P — открыть файл · Ctrl+B — старт");
            hint->labelsize(12);
            hint->labelcolor(theme::fg_dim());
            auto* add = new Fl_Button(70, 96, 200, 30, "+  Добавить файлы");
            add->labelfont(FL_BOLD);
            add->callback(add_cb);
            card->end();
            overlay->end();
            g.empty_state = overlay;
            g.btn_empty_add = add;

            // bottom panel
            {
                auto* bt = new Fl_Tabs(MAIN_X, TITLE_H + 51 + ed_h + 1, W - MAIN_X, BOTTOM_H - 1);
                g.bottom_tabs = bt;
                auto* pg = new Fl_Group(MAIN_X, TITLE_H + 51 + ed_h + 26, W - MAIN_X, BOTTOM_H - 27, "Проблемы");
                auto* pl = new Fl_Browser(MAIN_X + 8, TITLE_H + 51 + ed_h + 32, W - MAIN_X - 16, BOTTOM_H - 40);
                pl->textsize(12);
                pl->callback(prob_list_cb);
                pl->when(FL_WHEN_CHANGED);
                g.prob_list = pl;
                pg->end();
                auto* og = new Fl_Group(MAIN_X, TITLE_H + 51 + ed_h + 26, W - MAIN_X, BOTTOM_H - 27, "Вывод");
                auto* ob = new Fl_Text_Buffer();
                auto* od = new Fl_Text_Display(MAIN_X + 8, TITLE_H + 51 + ed_h + 32, W - MAIN_X - 16, BOTTOM_H - 40);
                od->buffer(ob);
                od->textfont(FL_COURIER);
                od->textsize(12);
                od->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
                g.out_disp = od;
                g.out_buf = ob;
                og->end();
                auto* ig = new Fl_Group(MAIN_X, TITLE_H + 51 + ed_h + 26, W - MAIN_X, BOTTOM_H - 27, "Ввод (stdin)");
                auto* ib = new Fl_Text_Buffer();
                auto* ie = new Fl_Text_Editor(MAIN_X + 8, TITLE_H + 51 + ed_h + 32, W - MAIN_X - 16, BOTTOM_H - 40);
                ie->buffer(ib);
                ie->textfont(FL_COURIER);
                ie->textsize(12);
                ie->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
                ib->text("");
                ie->tooltip("Данные для программы (stdin) — передаются python при запуске");
                g.stdin_ed = ie;
                g.stdin_buf = ib;
                ig->end();
                bt->end();
            }
            area->end();
        }
        main->end();
    }

    // ---- status bar ----
    {
        auto* st = new Fl_Group(0, H - STATUS_H, W, STATUS_H);
        st->box(FL_FLAT_BOX);
        auto* se = new Fl_Button(8, H - STATUS_H + 2, 110, 20, "✖ 0   ⚠ 0");
        se->box(FL_FLAT_BOX);
        se->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        se->labelsize(11);
        se->callback(status_err_cb);
        se->tooltip("Показать проблемы");
        g.status_err = se;
        auto* sr = new Fl_Box(W - 360, H - STATUS_H + 2, 352, 20, "Стр 1, стлб 1   UTF-8");
        sr->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        sr->labelsize(11);
        sr->labelcolor(theme::fg_dim());
        g.status_right = sr;
        st->end();
    }

    // ---- theme menu (kept as title-bar extra) ----
    // (activity → Настройки covers themes; the menu button is gone)

    win->end();

    // initial theme + session restore
    apply_theme_ui(ThemeMode::System);
    g.rb_dark->clear();
    g.rb_light->clear();
    g.rb_system->set();
    if (!g.store.projects.empty()) {
        g.cur_proj = 0;
        rebuild_browser();
        const Project& p = g.store.projects[0];
        if (!p.files.empty()) {
            fs::path target = p.last_open;
            bool ok = false;
            for (const auto& f : p.files) if (f == target) { ok = true; break; }
            if (!ok) target = p.files.front();
            extern_open(target.string());
        }
        rebuild_browser();
    }
    update_badges();
    update_status_lang();
    update_breadcrumb();
    update_empty_state();
    update_title();
    show_view(0);

    Fl::add_timeout(0.15, cursor_timer_cb, nullptr);

    win->show(argc, argv);
    int rc = Fl::run();
#ifdef _WIN32
    WSACleanup();
#endif
    return rc;
}
