/**
 * @file TuiKey.hpp
 * @brief TUI interactive keyboard codes.
 */
#pragma once

#include <cstdint>

namespace simrv::tui {

enum class TuiKey : uint8_t {
    CtrlB = 2,
    CtrlC = 3,
    CtrlD = 4,
    Tab = 9,
    Newline = 10,  // '\n'
    CtrlL = 12,
    Enter = 13,  // '\r'
    CtrlP = 16,
    CtrlQ = 17,
    CtrlR = 18,
    Space = 32,  // ' '
    BackTab = 254,
    a = 'a',
    A = 'A',
    c = 'c',
    C = 'C',
    e = 'e',
    E = 'E',
    h = 'h',
    H = 'H',
    q = 'q',
    Q = 'Q',
    r = 'r',
    R = 'R',
    s = 's',
    S = 'S',
    u = 'u',
    U = 'U',
    d = 'd',
    D = 'D',
    p = 'p',
    P = 'P',
    t = 't',
    T = 'T',
    v = 'v',
    V = 'V',
    x = 'x',
    X = 'X',
    b = 'b',
    B = 'B',
    n = 'n',
    N = 'N',
    i = 'i',
    I = 'I',
    j = 'j',
    J = 'J',
    k = 'k',
    K = 'K',
    l = 'l',
    L = 'L',
    m = 'm',
    M = 'M',
    o = 'o',
    O = 'O',
    Esc = 27,
    Backspace = 127,
    Colon = ':',
    QuestionMark = '?',
    f = 'f',
    F = 'F',
    w = 'w',
    W = 'W',
    g = 'g',
    G = 'G',
    Plus = '+',
    Minus = '-',
    Equal = '=',
    Comma = ',',
    Dot = '.',
    LeftBracket = '[',
    RightBracket = ']',
};

}  // namespace simrv::tui
