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
};

}  // namespace simrv::tui
