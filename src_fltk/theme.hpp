// language: C++20, module: theme — dark / light / system palettes
#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>

enum class ThemeMode { Dark, Light, System };

namespace theme {

// Applies the palette to FLTK globals, the syntax table, and every widget in
// the tree (recursive walk). Call Fl::redraw() afterwards (done here).
void apply(ThemeMode m, Fl_Widget* root);

// Resolves System against the OS preference; never fails — falls back to light.
bool resolves_dark(ThemeMode m);

// Palette access for widgets created after apply().
Fl_Color bg();        // window background
Fl_Color bg2();       // panels, buttons, browsers
Fl_Color fg();        // primary text
Fl_Color fg_dim();    // secondary text
Fl_Color sel();       // selection / accent
Fl_Color ed_bg();     // editor background
Fl_Color ed_fg();     // editor foreground
Fl_Color red();       // errors
Fl_Color green();     // success
Fl_Color act();       // activity bar / status bar chrome
Fl_Color side();      // sidebar background

} // namespace theme
