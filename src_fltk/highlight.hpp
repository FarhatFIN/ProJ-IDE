// language: C++20, module: highlight — Fl_Text_Editor syntax coloring
#pragma once

#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include "lang.hpp"

namespace hl {

// Rebuilds the style buffer for the whole text with a full-pass lexer.
// Style bytes: 'A' plain, 'B' keyword, 'C' string, 'D' comment, 'E' number,
// 'F' preprocessor. Files above 512 KB stay plain on purpose.
void restyle(Fl_Text_Buffer* text, Fl_Text_Buffer* style, Lang lang);

// Style table for Fl_Text_Display::highlight_data (index = style byte - 'A').
const Fl_Text_Display::Style_Table_Entry* style_table(int& count);

// Theme hook: recolors the style table in place; caller redraws the editor.
void apply_theme(bool dark);

} // namespace hl
