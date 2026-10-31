// language: C++20, file: main.cpp (src_fltk) — ProJ IDE native UI, FLTK 1.3.11
// Fully native: no HTTP, no HTML. Engine: src/ (build, store, install, liveproc).
// Features: projects, tabs, live run I/O, gdb/pdb debugger, real-time lint,
// GitHub + directory import, themes, palette, save-on-exit dialog.
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
#include <FL/fl_ask.H>

#include "util.hpp"
#include "lang.hpp"
#include "build.hpp"
#include "store.hpp"
#include "highlight.hpp"
#include "theme.hpp"
#include "install.hpp"
#include "liveproc.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------- CodeEditor

// Fl_Text_Editor with a themed line-number gutter (Debian FLTK ships the old
// ABI where linenumber color setters are no-ops — repaint over the base draw).
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

struct ErrRef { std::string path; int line = 0; int col = 0; };

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
    std::vector<Row> rows;
    std::vector<ErrRef> err_refs;

    std::vector<BuildError> diags;
    int err_count = 0, warn_count = 0;

    // git import handshake between the worker thread and the modal loop
    std::mutex import_mtx;
    bool git_done = false;
    bool git_result_ok = false;
    std::string git_result_detail;
    std::string git_result_name;
    std::vector<fs::path> git_result_files;

    // widgets
    Fl_Double_Window* win = nullptr;
    Fl_Group* titlebar = nullptr;
    Fl_Group* activity = nullptr;
    Fl_Button* act_explorer = nullptr;
    Fl_Button* act_run = nullptr;
    Fl_Button* act_settings = nullptr;
    Fl_Button* btn_palette = nullptr;
    Fl_Button* btn_start = nullptr;
    Fl_Button* btn_debug = nullptr;
    Fl_Group* sidebar = nullptr;
    Fl_Group* view_explorer = nullptr;
    Fl_Group* view_run = nullptr;
    Fl_Group* view_settings = nullptr;
    Fl_Browser* browser = nullptr;
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
    Fl_Group* proc_row = nullptr;
    Fl_Input* proc_input = nullptr;
    Fl_Button* btn_stop = nullptr;
    Fl_Button* status_err = nullptr;
    Fl_Box* status_right = nullptr;
    Fl_Round_Button* rb_dark = nullptr;
    Fl_Round_Button* rb_light = nullptr;
    Fl_Round_Button* rb_system = nullptr;

    Fl_Double_Window* pal_win = nullptr;
    Fl_Input* pal_input = nullptr;
    Fl_Browser* pal_list = nullptr;

    int last_line = -1, last_col = -1;
    bool restyle_pending = false;
    bool lint_busy = false;
    bool lint_pending = false;

    liveproc::LiveProc live;
    bool live_running = false;
} g;

static constexpr int W = 1200, H = 720;
static constexpr int ACT_W = 44;
static constexpr int SB_X = 44, SB_W = 264;
static constexpr int MAIN_X = 308;
static constexpr int TITLE_H = 32, STATUS_H = 24, BOTTOM_H = 200;
static constexpr int ERRBLOCK_H = 176;

static void out_append(const std::string& text);

// forward decls used across sections
static void select_tab(const std::string& path);
static void close_file(const std::string& path);
static void add_files_flow(int project_idx);
static void register_project_files(const std::string& name,
                                   const std::vector<fs::path>& files);
static void collect_text_files(const fs::path& dir, std::vector<fs::path>& out);
static void rebuild_browser();
void open_file(const std::string& path);

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

static void show_view(int which) {
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
    right += g.cur_file.empty() ? std::string("—")
                                : lang_name(lang_by_ext(fs::path(g.cur_file)));
    g.status_right->copy_label(right.c_str());
    g.status_right->redraw();
}

static void update_breadcrumb() {
    std::string b;
    if (g.cur_proj >= 0 && g.cur_proj < static_cast<int>(g.store.projects.size()))
        b = g.store.projects[g.cur_proj].name;
    if (!g.cur_file.empty()) {
        if (!b.empty()) b += "  ›  ";
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

// ---------------------------------------------------------------- output

static void out_append(const std::string& text) {
    g.out_buf->append(text.c_str());
    g.out_disp->insert_position(g.out_buf->length());
    g.out_disp->show_insert_position();
}

// ---------------------------------------------------------------- restyle + lint

static void lint_schedule();

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

static void lint_tick(void*);       // debounce fire → worker
static void lint_apply_tick(void*); // poll worker result

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
    lint_schedule();
}

// ---------------------------------------------------------------- diagnostics

static void fill_errors() {
    g.err_count = g.warn_count = 0;
    g.err_refs.clear();
    g.diag_list->clear();
    g.prob_list->clear();
    for (const auto& e : g.diags) {
        (e.is_error ? g.err_count : g.warn_count)++;
        const std::string icon = e.is_error ? "✖" : "⚠";
        std::string line = icon + "  ";
        if (e.line > 0) line += std::to_string(e.line) + ": ";
        line += e.msg;
        g.diag_list->add(line.c_str());
        g.prob_list->add((line + (e.col > 0 ? "  [" + std::to_string(e.line) + ", " +
                                    std::to_string(e.col) + "]" : "")).c_str());
        fs::path fp(e.file);
        if (fp.empty()) fp = g.cur_file;
        else if (fp.is_relative()) fp = fs::path(g.cur_file).parent_path() / fp;
        g.err_refs.push_back({fp.string(), e.line, e.col > 0 ? e.col : 1});
    }
    update_badges();
}

static void diag_click_common(int line) {
    if (line <= 0 || line > static_cast<int>(g.err_refs.size())) return;
    const auto& ref = g.err_refs[line - 1];
    if (ref.path.empty()) return;
    if (g.bufs.find(ref.path) == g.bufs.end()) open_file(ref.path);
    else select_tab(ref.path);
    if (ref.line > 0) goto_line(ref.path, ref.line, ref.col);
}

// ---------------------------------------------------------------- tabs / files

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
    g.diags.clear();
    fill_errors();
}

void open_file(const std::string& path) {
    if (g.bufs.count(path)) { select_tab(path); return; }

    auto buf = new Fl_Text_Buffer();
    if (buf->loadfile(path.c_str()) != 0) {
        delete buf;
        // file vanished from disk — offer to drop the dead reference
        const int r = fl_choice("Файл пропал с диска:\n%s\n\nУбрать его из проекта?",
                                "Оставить", "Убрать", nullptr,
                                fs::path(path).filename().string().c_str());
        if (r == 1)
            for (int i = 0; i < static_cast<int>(g.store.projects.size()); ++i) {
                bool had = false;
                for (const auto& f : g.store.projects[i].files)
                    if (f == fs::path(path)) { had = true; break; }
                if (had) {
                    g.store.remove_file(i, fs::path(path));
                    g.store.save();
                    rebuild_browser();
                    break;
                }
            }
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
    g.diags.clear();
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
        g.browser->add((std::to_string(i + 1) + ".  " + p.name).c_str());
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
        if (f.string() != g.cur_file) open_file(f.string());
        else select_tab(f.string());
    } else {
        g.cur_proj = row.proj;
        const Project& p = g.store.projects[row.proj];
        if (!p.files.empty()) {
            fs::path target = p.last_open;
            bool ok = false;
            for (const auto& f : p.files) if (f == target) { ok = true; break; }
            if (!ok) target = p.files.front();
            if (target.string() != g.cur_file) open_file(target.string());
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
        if (id == 3) {
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
        if (id == 1) {
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
            open_file(nf.string());
        }
    }
}

// ---------------------------------------------------------------- file dialog

// Native chooser replacement: fully FLTK directory browser, no GTK modules.
// Mode FILES — multi-select of files; mode DIR — pick a directory.
namespace filedlg {

struct State {
    Fl_Double_Window* win = nullptr;
    Fl_Input* path = nullptr;
    Fl_Browser* list = nullptr;
    std::vector<fs::path> result;
    fs::path cur;
    bool dir_mode = false;
} st;

void populate() {
    st.list->clear();
    if (st.cur.has_parent_path() && st.cur != st.cur.root_path())
        st.list->add("../");
    std::vector<fs::path> dirs, files;
    std::error_code ec;
    for (fs::directory_iterator it(st.cur, fs::directory_options::skip_permission_denied, ec), e;
         it != e; it.increment(ec)) {
        const fs::path p = it->path();
        if (it->is_directory(ec)) dirs.push_back(p);
        else {
            if (st.dir_mode) continue;
            std::ifstream f(p, std::ios::binary);
            char b[512] = {0};
            f.read(b, sizeof(b));
            if (f.gcount() > 0 && std::memchr(b, 0, static_cast<size_t>(f.gcount()))) continue;
            files.push_back(p);
        }
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    for (auto& d : dirs) st.list->add(("./" + d.filename().string() + "/").c_str());
    for (auto& f : files) st.list->add(f.filename().string().c_str());
    st.path->value(st.cur.string().c_str());
}

void confirm_dir() {
    st.result = {st.cur};
    st.win->hide();
}

void row_action(const char* text) {
    std::string t = text ? text : "";
    if (t == "../") {
        if (st.cur.has_parent_path()) st.cur = st.cur.parent_path();
        populate();
        return;
    }
    if (!t.empty() && t.back() == '/') {
        fs::path next = st.cur / t.substr(2, t.size() - 3);
        std::error_code ec;
        if (fs::is_directory(next, ec)) { st.cur = next; populate(); }
        return;
    }
    if (st.dir_mode) return;
    st.result = {st.cur / t};
    st.win->hide();
}

std::vector<fs::path> pick_files(Fl_Window* parent, const std::string& start) {
    st.result.clear();
    st.dir_mode = false;
    st.cur = start.empty() ? util::home_dir() : fs::path(start);
    if (!fs::exists(st.cur)) st.cur = util::home_dir();

    st.win = new Fl_Double_Window(600, 440, "Открыть файлы");
    st.win->set_modal();
    auto* pi = new Fl_Input(10, 10, 480, 24, "Путь:");
    pi->textfont(FL_COURIER); pi->textsize(11);
    st.path = pi;
    auto* home = new Fl_Button(498, 10, 44, 24, "Дом");
    auto* up = new Fl_Button(546, 10, 44, 24, "Вверх");
    auto* list = new Fl_Browser(10, 42, 580, 340);
    list->type(FL_MULTI_BROWSER);
    list->textfont(FL_COURIER); list->textsize(12);
    st.list = list;
    auto* ok = new Fl_Button(462, 396, 128, 28, "Выбрать");
    st.win->end();

    pi->callback([](Fl_Widget* w, void*) {
        if (Fl::event_key() != FL_Enter) return;
        fs::path p(util::trim(static_cast<Fl_Input*>(w)->value()));
        std::error_code ec;
        if (fs::is_directory(p, ec)) { st.cur = p; populate(); return; }
        if (fs::is_regular_file(p, ec) && !st.dir_mode) {
            st.result = {p};
            st.win->hide();
        }
    });
    pi->when(FL_WHEN_ENTER_KEY);
    home->callback([](Fl_Widget*, void*) { st.cur = util::home_dir(); populate(); });
    up->callback([](Fl_Widget*, void*) {
        if (st.cur.has_parent_path()) { st.cur = st.cur.parent_path(); populate(); }
    });
    list->callback([](Fl_Widget* w, void*) {
        auto* br = static_cast<Fl_Browser*>(w);
        if (Fl::event_clicks() > 0 || Fl::event_key() == FL_Enter)
            row_action(br->text(br->value()));
    });
    ok->callback([](Fl_Widget*, void*) {
        st.result.clear();
        for (int i = 1; i <= st.list->size(); ++i)
            if (st.list->selected(i)) {
                std::string t = st.list->text(i);
                if (t != "../" && (t.empty() || t.back() != '/'))
                    st.result.push_back(st.cur / t);
            }
        st.win->hide();
    });

    populate();
    st.win->show();
    while (st.win->visible_r()) Fl::wait();
    delete st.win;
    st.win = nullptr;
    return st.result;
}

std::optional<fs::path> pick_dir(Fl_Window* parent, const std::string& start) {
    st.result.clear();
    st.dir_mode = true;
    st.cur = start.empty() ? util::home_dir() : fs::path(start);
    if (!fs::exists(st.cur)) st.cur = util::home_dir();

    st.win = new Fl_Double_Window(600, 440, "Выбрать папку");
    st.win->set_modal();
    auto* pi = new Fl_Input(10, 10, 480, 24, "Путь:");
    pi->textfont(FL_COURIER); pi->textsize(11);
    st.path = pi;
    auto* home = new Fl_Button(498, 10, 44, 24, "Дом");
    auto* up = new Fl_Button(546, 10, 44, 24, "Вверх");
    auto* list = new Fl_Browser(10, 42, 580, 340);
    list->type(FL_HOLD_BROWSER);
    list->textfont(FL_COURIER); list->textsize(12);
    st.list = list;
    auto* ok = new Fl_Button(462, 396, 128, 28, "Эта папка");
    st.win->end();

    pi->callback([](Fl_Widget* w, void*) {
        if (Fl::event_key() != FL_Enter) return;
        fs::path p(util::trim(static_cast<Fl_Input*>(w)->value()));
        std::error_code ec;
        if (fs::is_directory(p, ec)) { st.cur = p; populate(); return; }
    });
    pi->when(FL_WHEN_ENTER_KEY);
    home->callback([](Fl_Widget*, void*) { st.cur = util::home_dir(); populate(); });
    up->callback([](Fl_Widget*, void*) {
        if (st.cur.has_parent_path()) { st.cur = st.cur.parent_path(); populate(); }
    });
    list->callback([](Fl_Widget* w, void*) {
        auto* br = static_cast<Fl_Browser*>(w);
        if (Fl::event_clicks() > 0 || Fl::event_key() == FL_Enter)
            row_action(br->text(br->value()));
    });
    ok->callback([](Fl_Widget*, void*) { confirm_dir(); });

    populate();
    st.win->show();
    while (st.win->visible_r()) Fl::wait();
    delete st.win;
    st.win = nullptr;
    if (st.result.empty()) return std::nullopt;
    return st.result.front();
}

} // namespace filedlg


// ---------------------------------------------------------------- imports

// text files of a directory tree; binaries and junk dirs skipped
static void collect_text_files(const fs::path& dir, std::vector<fs::path>& out) {
    static const char* skip[] = {".git", "node_modules", "__pycache__", "build",
                                 "dist", ".idea", ".vscode", "cmake-build-debug"};
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    while (it != end) {
        const fs::path p = it->path();
        const std::string fn = p.filename().string();
        bool skipn = false;
        for (const char* sname : skip) if (fn == sname) { skipn = true; break; }
        if (it->is_directory(ec)) {
            if (skipn) it.disable_recursion_pending();
        } else if (!skipn) {
            auto sz = it->file_size(ec);
            if (!ec && sz > 0 && sz <= 4 * 1024 * 1024) {
                std::ifstream f(p, std::ios::binary);
                char b[512] = {0};
                f.read(b, sizeof(b));
                if (f.gcount() <= 0 || !std::memchr(b, 0, static_cast<size_t>(f.gcount())))
                    out.push_back(p);
            }
        }
        it.increment(ec);
    }
}

static void register_project_files(const std::string& name,
                                   const std::vector<fs::path>& files) {
    g.store.add_project(name, files);
    g.cur_proj = static_cast<int>(g.store.projects.size()) - 1;
    g.store.save();
    rebuild_browser();
    if (!files.empty()) open_file(files.front().string());
    update_title();
    update_empty_state();
}

static void import_git_dialog() {
    fs::path gitp;
    if (!util::find_in_path("git", gitp)) {
        fl_alert("git не найден в PATH.\nУстанови: sudo apt install git");
        return;
    }
    Fl_Window dlg(480, 140, "Импорт репозитория GitHub");
    dlg.set_modal();
    Fl_Box lab(16, 14, 448, 22, "URL репозитория (https://github.com/...):");
    lab.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    Fl_Input in(16, 40, 448, 26);
    in.value("https://github.com/");
    auto* okbtn = new Fl_Button(360, 78, 104, 26, "Клонировать");
    okbtn->callback([](Fl_Widget* w, void*) {
        w->window()->user_data((void*)1);
        w->window()->hide();
    });
    dlg.end();

    dlg.show();
    while (dlg.visible_r()) Fl::wait();
    if ((intptr_t)dlg.user_data() != 1) return;
    std::string url = util::trim(in.value());
    if (url.empty()) return;

    std::string name = url.substr(url.find_last_of('/') + 1);
    if (name.size() > 4 && name.rfind(".git") == name.size() - 4)
        name.resize(name.size() - 4);
    if (name.empty()) name = "repo";

    std::atomic<bool> done{false};
    std::atomic<pid_t> clone_pid{-1};
    std::thread worker([&]() {
        fs::path target = util::home_dir() / "ProJ-IDE-Projects" / name;
        std::error_code ec;
        fs::remove_all(target, ec);
        // background git with its pid exposed so Отмена can kill it
        std::string cmd = "git clone --depth 1 '" + url + "' '" + target.string() + "'";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) {
            std::lock_guard<std::mutex> lk(g.import_mtx);
            g.git_result_ok = false;
            g.git_result_detail = "git не запустился";
            done.store(true);
            return;
        }
        char buf[256];
        std::string out;
        while (fgets(buf, sizeof(buf), p)) out += buf;
        int rc = pclose(p);
        std::vector<fs::path> files;
        if (rc == 0) collect_text_files(target, files);
        std::lock_guard<std::mutex> lk(g.import_mtx);
        g.git_result_ok = (rc == 0);
        g.git_result_detail = rc == 0 ? "" : (out.empty() ? "git clone не удался" : out);
        g.git_result_files = files;
        g.git_result_name = name;
        done.store(true);
    });
    worker.detach();

    Fl_Window prog(380, 120, "Клонирование");
    prog.set_modal();
    Fl_Box st(16, 14, 348, 22, "git clone…");
    st.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    Fl_Progress bar(16, 42, 348, 18);
    bar.minimum(0); bar.maximum(1); bar.value(0);
    auto* cancel = new Fl_Button(255, 78, 108, 26, "Отмена");
    cancel->callback([](Fl_Widget* w, void*) {
        // kill any running git clone of this session
        util::exec_capture({"pkill", "-f", "git clone --depth 1"}, std::nullopt, 5);
        w->window()->user_data((void*)2);
    });
    prog.end();
    prog.show();
    double pulse = 0;
    while (!done.load()) {
        Fl::wait(0.08);
        pulse = std::fmod(pulse + 0.03, 1.0);
        bar.value(pulse);
        if ((intptr_t)prog.user_data() == 2) {
            util::exec_capture({"pkill", "-f", "git clone --depth 1"}, std::nullopt, 5);
        }
    }
    prog.hide();
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // pclose завершился

    bool ok = false;
    std::vector<fs::path> files;
    std::string detail;
    {
        std::lock_guard<std::mutex> lk(g.import_mtx);
        ok = g.git_result_ok;
        files = g.git_result_files;
        detail = g.git_result_detail;
        g.git_done = false;
    }
    if (!ok) {
        if ((intptr_t)prog.user_data() == 2)
            fl_alert("Клонирование отменено.");
        else
            fl_alert("Не удалось клонировать:\n%s", detail.c_str());
        return;
    }
    // re-import of an existing name replaces its file list
    bool exists = false;
    for (auto& prj : g.store.projects) if (prj.name == name) { exists = true; break; }
    if (exists) {
        for (int i = 0; i < static_cast<int>(g.store.projects.size()); ++i)
            if (g.store.projects[i].name == name) {
                for (const auto& f : g.store.projects[i].files) close_file(f.string());
                g.store.remove_project(i);
                break;
            }
    }
    register_project_files(name, files);
}

static void github_import_cb(Fl_Widget*, void*) { import_git_dialog(); }

static void add_files_flow(int project_idx) {
    std::string start;
    if (g.cur_proj >= 0 && g.cur_proj < static_cast<int>(g.store.projects.size()) &&
        !g.store.projects[g.cur_proj].files.empty())
        start = g.store.projects[g.cur_proj].files.front().parent_path().string();
    std::vector<fs::path> files = filedlg::pick_files(g.win, start);
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
    if (!files.empty()) open_file(files.front().string());
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

static void palette_fill_enter(const std::string& row_text);

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
            if (Fl::event_key() == FL_Enter) {
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
            if (t) palette_fill_enter(std::string(t));
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

static void palette_fill_enter(const std::string& row_text) {
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
                open_file(f.string());
                if (g.pal_win) g.pal_win->hide();
                return;
            }
        }
    }
}

static void palette_cb(Fl_Widget*, void*) { palette_open(); }

// ---------------------------------------------------------------- build + live



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

static bool ensure_tool(const std::string& tool) {
    std::string ver;
    if (install::tool_present(tool, ver)) return true;
    std::string q = std::string("У вас не нашлось \"") + tool +
                    "\".\nМы можем скачать его из доверенных источников.";
    if (fl_choice(q.c_str(), "Выйти", "Разрешаю", nullptr) != 1) return false;
    return install_flow(tool);
}

static void show_panel(int which) { // 0 problems, 1 output
    if (which == 0) g.bottom_tabs->value(g.bottom_tabs->child(0));
    if (which == 1) g.bottom_tabs->value(g.bottom_tabs->child(1));
    g.bottom_tabs->redraw();
}

// ---- live process plumbing ----

static void live_poll_cb(void*) {
    std::string chunk;
    const bool running = g.live.read_chunk(chunk);
    if (!chunk.empty()) out_append(chunk);
    if (running) {
        Fl::repeat_timeout(0.3, live_poll_cb);
    } else {
        g.live_running = false;
        g.proc_row->hide();
        out_append("\n■ программа завершилась\n");
        update_status_lang();
    }
}

static void live_run(const std::vector<std::string>& argv, const std::string& cwd,
                     const std::string& header) {
    if (!g.live.start(argv, cwd)) {
        out_append("\n✖ не удалось запустить процесс\n");
        return;
    }
    g.live_running = true;
    out_append(header);
    g.proc_row->show();
    g.proc_input->take_focus();
    update_status_lang();
    Fl::add_timeout(0.3, live_poll_cb);
}



static void start_cb_impl(bool debug_mode) {
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
    if (g.live_running) g.live.kill(); // новый запуск прибивает предыдущий
    do_save(true);

    const std::string tool = tool_for(lang);
    if (!ensure_tool(tool)) return;
    if (debug_mode && (lang == Lang::Cpp || lang == Lang::C)) {
        if (!ensure_tool("gdb")) return;
    }

    show_panel(1);
    out_append("▶ " + src.filename().string() + "\nСборка…\n");
    BuildResult res = build_file(src, "", debug_mode);
    out_append(res.log + "\n");
    g.diags = res.errors;
    fill_errors();

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
        return;
    }

    out_append("✔ Собрано\n");
    std::vector<std::string> argv;
    std::string cwd = src.parent_path().string();
    std::string header;
    if (debug_mode) {
        if (lang == Lang::Python) {
            argv = {"python3", "-m", "pdb", src.string()};
            header = "$ python3 -m pdb  (run · n · p var · bt · q)\n";
        } else {
            argv = {"gdb", "-q", res.produced.string()};
            header = "$ gdb  (run · break main · n · p var · bt · q)\n";
        }
    } else if (lang == Lang::Python) {
        argv = {"python3", "-X", "utf8", src.string()};
        header = "$ python3 " + src.filename().string() + "\n";
    } else {
        argv = {res.produced.string()};
        header = "$ " + res.produced.filename().string() + "\n";
    }
    live_run(argv, cwd, header);
}

static void start_cb3(Fl_Widget*, void*, bool debug_mode) { start_cb_impl(debug_mode); }

// ---------------------------------------------------------------- errors click

static void diag_list_cb(Fl_Widget* w, void*) {
    diag_click_common(static_cast<Fl_Browser*>(w)->value());
}

static void prob_list_cb(Fl_Widget* w, void*) {
    diag_click_common(static_cast<Fl_Browser*>(w)->value());
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
    const intptr_t mode = (intptr_t)data;
    apply_theme_ui(mode == 0 ? ThemeMode::Dark :
                   mode == 1 ? ThemeMode::Light : ThemeMode::System);
}

static void act_cb(Fl_Widget* /*w*/, void* data) {
    show_view(static_cast<int>((intptr_t)data));
}

// ---------------------------------------------------------------- quit

static void quit_cb(Fl_Widget*, void*) {
    bool any_dirty = false;
    for (auto& [path, d] : g.dirty) if (d) { any_dirty = true; break; }
    if (any_dirty) {
        const int r = fl_choice("Сохранить изменения перед выходом?",
                                "Не сохранять", "Сохранить", nullptr);
        if (r == 1)
            for (auto& [path, b] : g.bufs) b->savefile(path.c_str());
    }
    g.store.set_last(g.cur_proj, fs::path(g.cur_file));
    g.store.save();
    g.win->hide();
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

// ---------------------------------------------------------------- lint impl

static std::mutex g_lint_mtx;
static bool g_lint_ready = false;
static std::string g_lint_path;
static std::vector<BuildError> g_lint_diags;

static void lint_apply_tick(void*);

static void lint_tick(void*) {
    if (!g.lint_pending || g.lint_busy || g.cur_file.empty()) return;
    g.lint_pending = false;
    const std::string path = g.cur_file;
    auto b = g.bufs.find(path);
    if (b == g.bufs.end()) return;
    const std::string content = b->second->text();
    g.lint_busy = true;
    std::thread([path, content]() {
        auto diags = lint_file(fs::path(path), content);
        std::lock_guard<std::mutex> lk(g_lint_mtx);
        g_lint_path = path;
        g_lint_diags = std::move(diags);
        g_lint_ready = true;
    }).detach();
    Fl::add_timeout(0.08, lint_apply_tick);
}

static void lint_apply_tick(void*) {
    bool ready = false;
    {
        std::lock_guard<std::mutex> lk(g_lint_mtx);
        ready = g_lint_ready;
    }
    if (!ready) { Fl::add_timeout(0.08, lint_apply_tick); return; }

    std::string path;
    std::vector<BuildError> diags;
    {
        std::lock_guard<std::mutex> lk(g_lint_mtx);
        path = g_lint_path;
        diags = g_lint_diags;
        g_lint_ready = false;
    }
    g.lint_busy = false;

    if (path != g.cur_file) return;   // switched away — drop
    if (!g.diags.empty()) return;     // build errors on screen take priority
    g.diags = diags;
    fill_errors();
}

static void lint_schedule() {
    g.lint_pending = true;
    Fl::remove_timeout(lint_tick);
    Fl::add_timeout(1.0, lint_tick);
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    Fl::scheme("gtk+");
    g.store.load();
    // prune references to files that no longer exist on disk
    for (auto& prj : g.store.projects) {
        std::vector<fs::path> alive;
        for (const auto& f : prj.files)
            if (fs::exists(f)) alive.push_back(f);
        if (alive.size() != prj.files.size()) prj.files = alive;
        if (!prj.last_open.empty() && !fs::exists(prj.last_open))
            prj.last_open = prj.files.empty() ? fs::path{} : prj.files.front();
    }
    g.store.save();

    auto* win = new Fl_Double_Window(W, H, "ProJ IDE");
    g.win = win;
    win->size_range(900, 560);
    win->callback(quit_cb);

    {
        auto* tb = new Fl_Group(0, 0, W, TITLE_H);
        tb->box(FL_FLAT_BOX);
        g.titlebar = tb;
        auto* logo = new Fl_Box(8, 4, 76, 24, "ProJ IDE");
        logo->labelfont(FL_BOLD + FL_COURIER);
        logo->labelsize(13);
        auto* pal = new Fl_Button(120, 5, 300, 22, "Поиск файлов…   Ctrl+P");
        pal->box(FL_THIN_UP_BOX);
        pal->labelsize(11);
        pal->shortcut(FL_COMMAND + 'p');
        pal->callback(palette_cb);
        auto* sv = new Fl_Button(W - 344, 5, 104, 22, "Сохранить");
        sv->shortcut(FL_COMMAND + 's');
        sv->tooltip("Ctrl+S");
        sv->callback(save_cb);
        auto* dbg = new Fl_Button(W - 232, 5, 104, 22, "Отладка");
        dbg->tooltip("Собрать с -g -O0 и открыть gdb (python — pdb)");
        dbg->callback([](Fl_Widget*, void*) { start_cb_impl(true); });
        auto* start = new Fl_Button(W - 120, 5, 112, 22, "▶  Запуск");
        start->shortcut(FL_COMMAND + 'b');
        start->labelfont(FL_BOLD);
        start->tooltip("Собрать и выполнить (Ctrl+B)");
        start->callback([](Fl_Widget*, void*) { start_cb_impl(false); });
        g.btn_palette = pal;
        g.btn_debug = dbg;
        g.btn_start = start;
        tb->end();
    }

    {
        auto* act = new Fl_Group(0, TITLE_H, ACT_W, H - TITLE_H - STATUS_H);
        act->box(FL_FLAT_BOX);
        g.activity = act;
        const auto mk = [&](int y, const char* label, intptr_t id, const char* tip) {
            auto* b = new Fl_Button(0, TITLE_H + y, ACT_W, 40, label);
            b->box(FL_FLAT_BOX);
            b->labelsize(16);
            b->tooltip(tip);
            b->callback(act_cb, (void*)id);
            return b;
        };
        g.act_explorer = mk(6, "☰", 0, "Проводник");
        g.act_run = mk(50, "▶", 1, "Запуск и отладка");
        g.act_settings = mk(94, "⚙", 2, "Настройки");
        act->end();
    }

    {
        auto* sb = new Fl_Group(SB_X, TITLE_H, SB_W, H - TITLE_H - STATUS_H);
        sb->box(FL_FLAT_BOX);
        g.sidebar = sb;

        const int vh = H - TITLE_H - STATUS_H - ERRBLOCK_H;
        {
            auto* v = new Fl_Group(SB_X, TITLE_H, SB_W, vh);
            v->box(FL_FLAT_BOX);
            g.view_explorer = v;
            auto* hdr = new Fl_Box(SB_X + 8, TITLE_H + 4, 100, 20, "ПРОЕКТЫ");
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
            auto* gi = new Fl_Button(SB_X + 6, TITLE_H + 26, (SB_W - 18) / 2, 22, "Импорт GitHub");
            gi->box(FL_THIN_UP_BOX);
            gi->labelsize(10);
            gi->tooltip("git clone репозитория — все текстовые файлы в проект");
            gi->callback([](Fl_Widget*, void*) { import_git_dialog(); });
            auto* di = new Fl_Button(SB_X + 12 + (SB_W - 18) / 2, TITLE_H + 26,
                                     (SB_W - 18) / 2, 22, "Папка");
            di->box(FL_THIN_UP_BOX);
            di->labelsize(10);
            di->tooltip("Импорт целой папки (все текстовые файлы)");
            di->callback([](Fl_Widget*, void*) {
                auto dirv = filedlg::pick_dir(g.win, "");
                if (!dirv) return;
                fs::path dir = *dirv;
                std::vector<fs::path> files;
                collect_text_files(dir, files);
                if (files.empty()) {
                    fl_alert("В папке нет текстовых файлов исходников:\n%s",
                             dir.string().c_str());
                    return;
                }
                register_project_files(dir.filename().string(), files);
            });
            auto* br = new Fl_Browser(SB_X, TITLE_H + 52, SB_W, vh - 52);
            br->when(FL_WHEN_RELEASE);  // every click acts, even on the selected row
            br->callback(browser_cb);
            br->textsize(12);
            br->tooltip("Проекты. Клик — раскрыть и открыть последний файл. ПКМ — действия.");
            g.browser = br;
            v->end();

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

        {
            const int vh2 = H - TITLE_H - STATUS_H;
            auto* v = new Fl_Group(SB_X, TITLE_H, SB_W, vh2);
            v->box(FL_FLAT_BOX);
            g.view_run = v;
            v->hide();
            auto* hdr = new Fl_Box(SB_X + 12, TITLE_H + 8, SB_W - 24, 20, "ЗАПУСК И ОТЛАДКА");
            hdr->labelfont(FL_BOLD);
            hdr->labelsize(10);
            hdr->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            hdr->labelcolor(theme::fg_dim());
            auto* st = new Fl_Button(SB_X + 12, TITLE_H + 34, SB_W - 24, 28, "▶  Запуск");
            st->labelfont(FL_BOLD);
            st->callback([](Fl_Widget*, void*) { start_cb_impl(false); });
            st->shortcut(FL_COMMAND + 'b');
            auto* trm = new Fl_Button(SB_X + 12, TITLE_H + 68, SB_W - 24, 24, "Отладка (gdb / pdb)");
            trm->callback([](Fl_Widget*, void*) { start_cb_impl(true); });
            trm->tooltip("Сборка с -g -O0 и отладчик в панели «Вывод»");
            auto* info = new Fl_Box(SB_X + 12, TITLE_H + 100, SB_W - 24, 170,
                "C++    g++ -std=c++20 -Wall -Wextra\n"
                "C      gcc -std=c17 -Wall -Wextra\n"
                "Python py_compile + запуск\n"
                "NASM   nasm + gcc\n\n"
                "Программа живёт в панели «Вывод»:\n"
                "вывод стримится, ввод — в строке\n"
                "внизу панели, Enter отправляет.\n\n"
                "gdb: run · break main · n · p var · bt · q\n"
                "pdb: run · n · p var · b 12 · c · q");
            info->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_TOP | FL_ALIGN_WRAP);
            info->labelsize(11);
            info->labelcolor(theme::fg_dim());
            auto* keys = new Fl_Box(SB_X + 12, TITLE_H + vh2 - 66, SB_W - 24, 56,
                "Ctrl+B — запуск · Ctrl+S — сохранить\nCtrl+P — найти файл");
            keys->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_TOP | FL_ALIGN_WRAP);
            keys->labelsize(11);
            keys->labelcolor(theme::fg_dim());
            v->end();
        }

        {
            const int vh3 = H - TITLE_H - STATUS_H;
            auto* v = new Fl_Group(SB_X, TITLE_H, SB_W, vh3);
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

    {
        auto* main = new Fl_Group(MAIN_X, TITLE_H, W - MAIN_X, H - TITLE_H - STATUS_H);
        win->resizable(main);

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

        {
            auto* bc = new Fl_Box(MAIN_X + 12, TITLE_H + 31, W - MAIN_X - 24, 18, "");
            bc->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            bc->labelsize(11);
            bc->labelcolor(theme::fg_dim());
            g.breadcrumb = bc;
        }

        {
            const int ed_h = H - TITLE_H - STATUS_H - 50 - BOTTOM_H;
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
                                    "Ctrl+P — открыть файл · Ctrl+B — запуск");
            hint->labelsize(12);
            hint->labelcolor(theme::fg_dim());
            auto* add = new Fl_Button(70, 96, 200, 30, "+  Добавить файлы");
            add->labelfont(FL_BOLD);
            add->callback(add_cb);
            card->end();
            overlay->end();
            g.empty_state = overlay;
            g.btn_empty_add = add;

            {
                const int tabs_y = TITLE_H + 51 + ed_h + 1;
                auto* bt = new Fl_Tabs(MAIN_X, tabs_y, W - MAIN_X, BOTTOM_H - 26);
                g.bottom_tabs = bt;
                const int inner_h = BOTTOM_H - 26 - 27;
                auto* pg = new Fl_Group(MAIN_X, tabs_y + 25, W - MAIN_X, inner_h, "Проблемы");
                auto* pl = new Fl_Browser(MAIN_X + 8, tabs_y + 31, W - MAIN_X - 16, inner_h - 8);
                pl->textsize(12);
                pl->callback(prob_list_cb);
                pl->when(FL_WHEN_CHANGED);
                g.prob_list = pl;
                pg->end();
                auto* og = new Fl_Group(MAIN_X, tabs_y + 25, W - MAIN_X, inner_h, "Вывод");
                auto* ob = new Fl_Text_Buffer();
                auto* od = new Fl_Text_Display(MAIN_X + 8, tabs_y + 31, W - MAIN_X - 16, inner_h - 8);
                od->buffer(ob);
                od->textfont(FL_COURIER);
                od->textsize(12);
                od->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
                g.out_disp = od;
                g.out_buf = ob;
                og->end();
                bt->end();

                auto* prow = new Fl_Group(MAIN_X, tabs_y + BOTTOM_H - 26, W - MAIN_X, 25);
                prow->box(FL_FLAT_BOX);
                g.proc_row = prow;
                auto* pi = new Fl_Input(MAIN_X + 8, tabs_y + BOTTOM_H - 22,
                                        W - MAIN_X - 116, 21, "ввод программе:");
                pi->box(FL_THIN_DOWN_BOX);
                pi->textfont(FL_COURIER);
                pi->textsize(12);
                pi->when(FL_WHEN_ENTER_KEY);
                pi->tooltip("Enter отправляет строку программе в панели «Вывод»");
                pi->deactivate();
                g.proc_input = pi;
                auto* stop = new Fl_Button(W - 102, tabs_y + BOTTOM_H - 23, 94, 22, "■ Стоп");
                stop->box(FL_THIN_UP_BOX);
                stop->labelcolor(theme::red());
                stop->tooltip("Завершить программу");
                stop->deactivate();
                stop->callback([](Fl_Widget*, void*) { g.live.kill(); });
                g.btn_stop = stop;
                prow->end();
            }
            area->end();
        }
        main->end();
    }

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
        auto* sr = new Fl_Box(W - 380, H - STATUS_H + 2, 372, 20, "Стр 1, стлб 1   UTF-8");
        sr->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        sr->labelsize(11);
        sr->labelcolor(theme::fg_dim());
        g.status_right = sr;
        st->end();
    }

    win->end();

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
            open_file(target.string());
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
    return Fl::run();
}
