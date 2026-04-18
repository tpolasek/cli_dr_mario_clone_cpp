#include "terminal_io.h"

#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <poll.h>

static int read_byte() {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 1) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1)
            return static_cast<unsigned char>(c);
    }
    return -1;
}

static struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    std::cout << "\033[?25h";
    std::cout << "\033[?1049l";
    std::cout.flush();
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int poll_key() {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 0) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            int ch = static_cast<unsigned char>(c);
            // Arrow keys: ESC [ A/B/C/D
            if (ch == 0x1b) {
                int b1 = read_byte();
                if (b1 == '[') {
                    int b2 = read_byte();
                    switch (b2) {
                        case 'A': return KEY_UP;
                        case 'B': return KEY_DOWN;
                        case 'C': return KEY_RIGHT;
                        case 'D': return KEY_LEFT;
                        default: break;
                    }
                }
                // Not an arrow escape, discard (partial escape sequence)
            }
            return ch;
        }
    }
    return 0;
}
