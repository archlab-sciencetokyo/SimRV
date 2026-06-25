#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>

#define WIDTH 40
#define HEIGHT 20

struct termios orig_termios;

void reset_terminal_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    printf("\033[?25h"); // Show cursor
}

void set_conio_terminal_mode() {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(reset_terminal_mode);
    memcpy(&new_termios, &orig_termios, sizeof(struct termios));
    cfmakeraw(&new_termios);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    printf("\033[?25l"); // Hide cursor
}

int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int getch() {
    int r;
    unsigned char c;
    if ((r = read(STDIN_FILENO, &c, 1)) == 1) {
        return c;
    }
    return 0;
}

int main() {
    set_conio_terminal_mode();

    int snake_x[100] = {0}, snake_y[100] = {0};
    int snake_len = 4;
    int dir_x = 1, dir_y = 0;
    int food_x, food_y;
    int score = 0;
    int game_over = 0;

    // Initialize snake
    for (int i = 0; i < snake_len; i++) {
        snake_x[i] = WIDTH / 2 - i;
        snake_y[i] = HEIGHT / 2;
    }

    // Place first food
    srand(time(NULL));
    food_x = rand() % (WIDTH - 2) + 1;
    food_y = rand() % (HEIGHT - 2) + 1;

    char grid[HEIGHT][WIDTH + 1];

    while (!game_over) {
        // Handle input
        if (kbhit()) {
            int ch = getch();
            if (ch == 'q' || ch == 'Q' || ch == 3) { // 'q' or Ctrl-C
                break;
            }
            if ((ch == 'w' || ch == 'W') && dir_y != 1) {
                dir_x = 0; dir_y = -1;
            } else if ((ch == 's' || ch == 'S') && dir_y != -1) {
                dir_x = 0; dir_y = 1;
            } else if ((ch == 'a' || ch == 'A') && dir_x != 1) {
                dir_x = -1; dir_y = 0;
            } else if ((ch == 'd' || ch == 'D') && dir_x != -1) {
                dir_x = 1; dir_y = 0;
            }
        }

        // Move snake
        int next_x = snake_x[0] + dir_x;
        int next_y = snake_y[0] + dir_y;

        // Check boundary collisions
        if (next_x <= 0 || next_x >= WIDTH - 1 || next_y <= 0 || next_y >= HEIGHT - 1) {
            game_over = 1;
            break;
        }

        // Check self collisions
        for (int i = 0; i < snake_len; i++) {
            if (snake_x[i] == next_x && snake_y[i] == next_y) {
                game_over = 1;
                break;
            }
        }

        if (game_over) break;

        // Shift body
        for (int i = snake_len - 1; i > 0; i--) {
            snake_x[i] = snake_x[i - 1];
            snake_y[i] = snake_y[i - 1];
        }
        snake_x[0] = next_x;
        snake_y[0] = next_y;

        // Check if food eaten
        if (next_x == food_x && next_y == food_y) {
            score += 10;
            if (snake_len < 100) {
                snake_x[snake_len] = snake_x[snake_len - 1];
                snake_y[snake_len] = snake_y[snake_len - 1];
                snake_len++;
            }
            // Reposition food
            food_x = rand() % (WIDTH - 2) + 1;
            food_y = rand() % (HEIGHT - 2) + 1;
        }

        // Render grid
        // Clear grid
        for (int y = 0; y < HEIGHT; y++) {
            for (int x = 0; x < WIDTH; x++) {
                if (y == 0 || y == HEIGHT - 1 || x == 0 || x == WIDTH - 1) {
                    grid[y][x] = '#';
                } else {
                    grid[y][x] = ' ';
                }
            }
            grid[y][WIDTH] = '\0';
        }

        // Draw food
        grid[food_y][food_x] = '*';

        // Draw snake
        grid[snake_y[0]][snake_x[0]] = '@';
        for (int i = 1; i < snake_len; i++) {
            grid[snake_y[i]][snake_x[i]] = 'o';
        }

        // Clear screen and draw
        printf("\033[H\033[J");
        printf("--- SimRV Snake Demo ---  Score: %d\r\n", score);
        for (int y = 0; y < HEIGHT; y++) {
            printf("%s\r\n", grid[y]);
        }
        printf("Controls: WASD to Move, Q to Quit\r\n");
        fflush(stdout);

        usleep(150000); // 150ms delay
    }

    printf("\033[H\033[JGame Over! Final Score: %d\r\n", score);
    return 0;
}
