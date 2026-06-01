/**
 * @file Ansi.hpp
 * @brief ANSI escape code abstractions for TUI rendering.
 */
#pragma once

namespace simrv::tui::ansi {

#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"

// Foreground colors (Standard)
#define ANSI_FG_BLACK "\033[30m"
#define ANSI_FG_RED "\033[31m"
#define ANSI_FG_GREEN "\033[32m"
#define ANSI_FG_YELLOW "\033[33m"
#define ANSI_FG_BLUE "\033[34m"
#define ANSI_FG_MAGENTA "\033[35m"
#define ANSI_FG_CYAN "\033[36m"
#define ANSI_FG_WHITE "\033[37m"

// Foreground colors (Bright/Intense)
#define ANSI_FG_BRIGHT_BLACK "\033[90m"
#define ANSI_FG_BRIGHT_RED "\033[91m"
#define ANSI_FG_BRIGHT_GREEN "\033[92m"
#define ANSI_FG_BRIGHT_YELLOW "\033[93m"
#define ANSI_FG_BRIGHT_BLUE "\033[94m"
#define ANSI_FG_BRIGHT_MAGENTA "\033[95m"
#define ANSI_FG_BRIGHT_CYAN "\033[96m"
#define ANSI_FG_BRIGHT_WHITE "\033[97m"

// Background colors
#define ANSI_BG_BLACK "\033[40m"
#define ANSI_BG_RED "\033[41m"
#define ANSI_BG_GREEN "\033[42m"
#define ANSI_BG_YELLOW "\033[43m"
#define ANSI_BG_BLUE "\033[44m"
#define ANSI_BG_MAGENTA "\033[45m"
#define ANSI_BG_CYAN "\033[46m"
#define ANSI_BG_WHITE "\033[47m"

// Combined styles used in SimRV TUI
#define ANSI_BG_YELLOW_FG_BLACK "\033[1;43;30m"
#define ANSI_BG_GREEN_FG_BLACK "\033[1;42;30m"
#define ANSI_BG_RED_FG_BLACK "\033[1;30;41m"
#define ANSI_BG_YELLOW_FG_BLACK_BLINK "\033[1;5;30;43m"

#define ANSI_BOLD_FG_BRIGHT_BLUE "\033[1;94m"
#define ANSI_BOLD_FG_BRIGHT_WHITE "\033[1;97m"
#define ANSI_BOLD_FG_BRIGHT_GREEN "\033[1;92m"
#define ANSI_BOLD_FG_CYAN "\033[1;36m"
#define ANSI_BOLD_FG_RED "\033[1;31m"
#define ANSI_BOLD_FG_BRIGHT_BLACK "\033[1;90m"
#define ANSI_BOLD_FG_BRIGHT_YELLOW "\033[1;93m"

// Terminal control sequences
#define ANSI_CLEAR_SCREEN "\033[2J"
#define ANSI_CURSOR_HOME "\033[H"
#define ANSI_HIDE_CURSOR "\033[?25l"
#define ANSI_SHOW_CURSOR "\033[?25h"
#define ANSI_ENTER_ALT_SCREEN "\033[?1049h"
#define ANSI_LEAVE_ALT_SCREEN "\033[?1049l"

}  // namespace simrv::tui::ansi
