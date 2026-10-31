// language: C++20, module: highlight — full-pass lexer per language
#include "highlight.hpp"

#include <string>
#include <algorithm>
#include <unordered_set>

namespace hl {

namespace {

const std::unordered_set<std::string>& cpp_keywords() {
    static const std::unordered_set<std::string> k = {
        "alignas","alignof","auto","bool","break","case","catch","char","class","const",
        "constexpr","const_cast","continue","decltype","default","delete","do","double",
        "dynamic_cast","else","enum","explicit","export","extern","false","float","for",
        "friend","goto","if","inline","int","long","mutable","namespace","new","noexcept",
        "nullptr","NULL","operator","override","final","private","protected","public",
        "register","reinterpret_cast","return","short","signed","sizeof","static",
        "static_assert","static_cast","struct","switch","template","this","throw","true",
        "try","typedef","typeid","typename","union","unsigned","using","virtual","void",
        "volatile","wchar_t","while",
    };
    return k;
}

const std::unordered_set<std::string>& py_keywords() {
    static const std::unordered_set<std::string> k = {
        "and","as","assert","async","await","break","case","class","continue","def","del",
        "elif","else","except","False","finally","for","from","global","if","import","in",
        "is","lambda","match","None","nonlocal","not","or","pass","raise","return","True",
        "try","while","with","yield",
    };
    return k;
}

const std::unordered_set<std::string>& asm_keywords() {
    static const std::unordered_set<std::string> k = {
        // registers
        "rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp","eax","ebx","ecx","edx","esi",
        "edi","ebp","esp","ax","bx","cx","dx","si","di","bp","sp","al","bl","cl","dl",
        "ah","bh","ch","dh","r8","r9","r10","r11","r12","r13","r14","r15",
        // instructions
        "mov","movzx","movsx","lea","add","sub","mul","imul","div","idiv","inc","dec",
        "neg","not","and","or","xor","shl","shr","sar","sal","cmp","test","push","pop",
        "call","ret","jmp","je","jne","jz","jnz","jg","jl","jge","jle","ja","jb","jae",
        "jbe","js","jns","jo","jno","loop","leave","enter","nop","int","syscall","hlt",
        "xchg","xadd","cdq","cqo","cwde","pushf","popf","endbr64",
        // directives
        "section","global","extern","bits","db","dw","dd","dq","dt","resb","resw","resd",
        "resq","equ","times","align","incbin","org","default","absolute","common",
    };
    return k;
}

inline bool ident_start(char c) {
    return isalpha(static_cast<unsigned char>(c)) || c == '_';
}
inline bool ident_char(char c) {
    return isalnum(static_cast<unsigned char>(c)) || c == '_';
}
inline bool num_char(char c) {
    return isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_';
}

// ---- C family ----
std::string style_c_family(const std::string& t, bool) {
    std::string s(t.size(), 'A');
    const size_t n = t.size();
    bool in_block = false;   // /* */
    bool in_str = false;     // "
    bool in_chr = false;     // '
    size_t i = 0;
    while (i < n) {
        // line position for preprocessor detection
        size_t line_end = t.find('\n', i);
        if (line_end == std::string::npos) line_end = n;
        size_t ls = i, le = line_end;

        if (in_block) {
            // consume until */ possibly across lines
            size_t close = t.find("*/", i);
            if (close == std::string::npos) {
                std::fill(s.begin() + ls, s.begin() + n, 'D');
                return s;
            }
            std::fill(s.begin() + ls, s.begin() + close + 2, 'D');
            i = close + 2;
            in_block = false;
            continue;
        }

        // preprocessor line?
        size_t p = ls;
        while (p < le && (t[p] == ' ' || t[p] == '\t')) ++p;
        if (p < le && t[p] == '#') {
            std::fill(s.begin() + ls, s.begin() + le, 'F');
            // still mark comments/strings inside the line roughly: keep simple
            i = le < n ? le + 1 : n;
            continue;
        }

        // walk the line
        while (i < le) {
            char c = t[i];
            if (in_str) {
                size_t j = i;
                while (j < le) {
                    if (t[j] == '\\') { j += 2; continue; }
                    if (t[j] == '"') { ++j; break; }
                    ++j;
                }
                std::fill(s.begin() + i, s.begin() + std::min(j, n), 'C');
                i = j;
                in_str = false;
                continue;
            }
            if (in_chr) {
                size_t j = i;
                while (j < le) {
                    if (t[j] == '\\') { j += 2; continue; }
                    if (t[j] == '\'') { ++j; break; }
                    ++j;
                }
                std::fill(s.begin() + i, s.begin() + std::min(j, n), 'C');
                i = j;
                in_chr = false;
                continue;
            }
            if (c == '/' && i + 1 < le && t[i + 1] == '/') {
                std::fill(s.begin() + i, s.begin() + le, 'D');
                i = le;
                continue;
            }
            if (c == '/' && i + 1 < n && t[i + 1] == '*') {
                size_t close = t.find("*/", i + 2);
                if (close == std::string::npos) {
                    std::fill(s.begin() + i, s.begin() + n, 'D');
                    in_block = true;
                    i = n;
                } else {
                    std::fill(s.begin() + i, s.begin() + close + 2, 'D');
                    i = close + 2;
                }
                continue;
            }
            if (c == '"') { in_str = true; continue; }
            if (c == '\'') { in_chr = true; continue; }
            if (isdigit(static_cast<unsigned char>(c))) {
                size_t j = i + 1;
                while (j < le && num_char(t[j])) ++j;
                std::fill(s.begin() + i, s.begin() + j, 'E');
                i = j;
                continue;
            }
            if (ident_start(c)) {
                size_t j = i + 1;
                while (j < le && ident_char(t[j])) ++j;
                if (cpp_keywords().count(t.substr(i, j - i)))
                    std::fill(s.begin() + i, s.begin() + j, 'B');
                i = j;
                continue;
            }
            ++i;
        }
        if (i < n) ++i; // the newline itself
        in_str = in_chr = false; // unterminated strings do not span lines
    }
    return s;
}

// ---- Python ----
std::string style_python(const std::string& t) {
    std::string s(t.size(), 'A');
    const size_t n = t.size();
    bool in_triple = false;
    char tch = 0;
    size_t i = 0;
    while (i < n) {
        size_t le = t.find('\n', i);
        if (le == std::string::npos) le = n;

        if (in_triple) {
            std::string needle(3, tch);
            size_t close = t.find(needle, i);
            if (close == std::string::npos) {
                std::fill(s.begin() + i, s.begin() + n, 'C');
                return s;
            }
            std::fill(s.begin() + i, s.begin() + close + 3, 'C');
            i = close + 3;
            in_triple = false;
            continue;
        }

        size_t j = i;
        while (j < le) {
            char c = t[j];
            if (c == '#') { std::fill(s.begin() + j, s.begin() + le, 'D'); j = le; break; }
            if ((c == '"' || c == '\'')) {
                bool triple = j + 2 < le && t[j + 1] == c && t[j + 2] == c;
                if (triple) {
                    // triple may close on the same line
                    std::string needle(3, c);
                    size_t close = t.find(needle, j + 3);
                    if (close != std::string::npos && close < le) {
                        std::fill(s.begin() + j, s.begin() + close + 3, 'C');
                        j = close + 3;
                    } else {
                        std::fill(s.begin() + j, s.begin() + n, 'C');
                        in_triple = true; tch = c;
                        j = n;
                    }
                } else {
                    size_t k2 = j + 1;
                    while (k2 < le) {
                        if (t[k2] == '\\') { k2 += 2; continue; }
                        if (t[k2] == c) { ++k2; break; }
                        ++k2;
                    }
                    std::fill(s.begin() + j, s.begin() + std::min(k2, le), 'C');
                    j = k2;
                }
                continue;
            }
            if (isdigit(static_cast<unsigned char>(c))) {
                size_t k2 = j + 1;
                while (k2 < le && num_char(t[k2])) ++k2;
                std::fill(s.begin() + j, s.begin() + k2, 'E');
                j = k2;
                continue;
            }
            if (ident_start(c)) {
                size_t k2 = j + 1;
                while (k2 < le && ident_char(t[k2])) ++k2;
                if (py_keywords().count(t.substr(j, k2 - j)))
                    std::fill(s.begin() + j, s.begin() + k2, 'B');
                j = k2;
                continue;
            }
            ++j;
        }
        i = j < n ? j + 1 : n; // skip newline
    }
    return s;
}

// ---- Assembler (nasm) ----
std::string style_asm(const std::string& t) {
    std::string s(t.size(), 'A');
    const size_t n = t.size();
    size_t i = 0;
    while (i < n) {
        size_t le = t.find('\n', i);
        if (le == std::string::npos) le = n;
        size_t j = i;
        while (j < le) {
            char c = t[j];
            if (c == ';') { std::fill(s.begin() + j, s.begin() + le, 'D'); break; }
            if (c == '"' || c == '\'') {
                size_t k2 = j + 1;
                while (k2 < le) {
                    if (t[k2] == '\\') { k2 += 2; continue; }
                    if (t[k2] == c) { ++k2; break; }
                    ++k2;
                }
                std::fill(s.begin() + j, s.begin() + std::min(k2, le), 'C');
                j = k2;
                continue;
            }
            if (isdigit(static_cast<unsigned char>(c)) ||
                (c == '0' && j + 1 < le && (t[j + 1] == 'x' || t[j + 1] == 'X'))) {
                size_t k2 = j + 1;
                while (k2 < le && num_char(t[k2])) ++k2;
                std::fill(s.begin() + j, s.begin() + k2, 'E');
                j = k2;
                continue;
            }
            if (ident_start(c)) {
                size_t k2 = j + 1;
                while (k2 < le && ident_char(t[k2])) ++k2;
                if (asm_keywords().count(t.substr(j, k2 - j)))
                    std::fill(s.begin() + j, s.begin() + k2, 'B');
                j = k2;
                continue;
            }
            ++j;
        }
        i = le < n ? le + 1 : n;
    }
    return s;
}

// ---- Markdown ----
std::string style_markdown(const std::string& t) {
    std::string s(t.size(), 'A');
    const size_t n = t.size();
    size_t i = 0;
    bool in_fence = false;
    while (i < n) {
        size_t le = t.find('\n', i);
        if (le == std::string::npos) le = n;
        std::string line = t.substr(i, le - i);
        bool fence = line.size() >= 3 && line.compare(0, 3, "```") == 0;
        if (in_fence) {
            std::fill(s.begin() + i, s.begin() + le, 'C');
            if (fence) in_fence = false;
        } else if (fence) {
            std::fill(s.begin() + i, s.begin() + le, 'F');
            in_fence = true;
        } else if (!line.empty() && (line[0] == '#' ||
                   (line.size() > 1 && line[0] == '#' && line[1] == ' '))) {
            std::fill(s.begin() + i, s.begin() + le, 'B');
        } else {
            // inline `code` and **bold**
            for (size_t j = i; j < le;) {
                if (t[j] == '`') {
                    size_t close = t.find('`', j + 1);
                    if (close == std::string::npos || close > le) { ++j; continue; }
                    std::fill(s.begin() + j, s.begin() + close + 1, 'C');
                    j = close + 1;
                } else if (t[j] == '*' && j + 1 < le && t[j + 1] == '*') {
                    size_t close = t.find("**", j + 2);
                    if (close == std::string::npos || close >= le) { ++j; continue; }
                    std::fill(s.begin() + j, s.begin() + close + 2, 'B');
                    j = close + 2;
                } else {
                    ++j;
                }
            }
        }
        i = le < n ? le + 1 : n;
    }
    return s;
}

} // namespace

void restyle(Fl_Text_Buffer* text, Fl_Text_Buffer* style, Lang lang) {
    const std::string t(text->text(), text->length());
    std::string s;
    switch (lang) {
    case Lang::Cpp: case Lang::C:      s = style_c_family(t, lang == Lang::Cpp); break;
    case Lang::Python:                 s = style_python(t); break;
    case Lang::Asm:                    s = style_asm(t); break;
    case Lang::Markdown:               s = style_markdown(t); break;
    default:                           s.assign(t.size(), 'A'); break;
    }
    style->text(s.c_str());
}

const Fl_Text_Display::Style_Table_Entry* style_table(int& count) {
    static Fl_Text_Display::Style_Table_Entry table[] = {
        {0, FL_COURIER, 14, 0}, // A plain        (color set by theme)
        {0, FL_COURIER, 14, 0}, // B keyword
        {0, FL_COURIER, 14, 0}, // C string
        {0, FL_COURIER, 14, 0}, // D comment
        {0, FL_COURIER, 14, 0}, // E number
        {0, FL_COURIER, 14, 0}, // F preprocessor
    };
    count = 6;
    return table;
}

void apply_theme(bool dark) {
    static Fl_Text_Display::Style_Table_Entry* table = nullptr;
    int n = 0;
    style_table(n);
    table = const_cast<Fl_Text_Display::Style_Table_Entry*>(style_table(n));
    if (dark) {
        table[0].color = 0xdcdcdcff; // plain
        table[1].color = 0x569cd6ff; // keyword
        table[2].color = 0xce9178ff; // string
        table[3].color = 0x6a9955ff; // comment
        table[4].color = 0xb5cea8ff; // number
        table[5].color = 0xc586c0ff; // preprocessor
    } else {
        table[0].color = 0x202020ff;
        table[1].color = 0x0000ffff;
        table[2].color = 0xa31515ff;
        table[3].color = 0x008000ff;
        table[4].color = 0x098658ff;
        table[5].color = 0x7f0055ff;
    }
}

} // namespace hl
