/**
 * @file TuiKey.hpp
 * @brief TUI interactive keyboard codes.
 */
#pragma once

#include <cstdint>

namespace simrv::tui {

enum class TuiKey : uint8_t {
    CtrlC    = 3,
    Tab      = 9,
    Enter    = 13,   // '\r'
    Newline  = 10,   // '\n'
    CtrlP    = 16,
    CtrlQ    = 17,
    CtrlR    = 18,
    Space    = 32,   // ' '
    c        = 'c',
    C        = 'C',
    e        = 'e',
    E        = 'E',
    h        = 'h',
    H        = 'H',
    q        = 'q',
    Q        = 'Q',
    r        = 'r',
    R        = 'R',
    s        = 's',
    S        = 'S',
    u        = 'u',
    U        = 'U',
    d        = 'd',
    D        = 'D',
    p        = 'p',
    P        = 'P',
    t        = 't',
    T        = 'T',
    v        = 'v',
    V        = 'V',
    b        = 'b',
    B        = 'B',
    n        = 'n',
    N        = 'N',
    j        = 'j',
    J        = 'J',
    k        = 'k',
    K        = 'K',
    l        = 'l',
    L        = 'L',
    m        = 'm',
    M        = 'M',
    o        = 'o',
    O        = 'O',
    Esc      = 27,
    Backspace = 127,
    Colon    = ':',
    QuestionMark = '?',
    f        = 'f',
    F        = 'F',
    w        = 'w',
    W        = 'W',
    g        = 'g',
    G        = 'G',
    Plus     = '+',
    Minus    = '-',
    Equal    = '=',
    Comma    = ',',
    Dot      = '.',
    LeftBracket  = '[',
    RightBracket = ']',
};

}  // namespace simrv::tui
