#ifndef TERMINAL_IO_H
#define TERMINAL_IO_H

void disable_raw_mode();
void enable_raw_mode();
int poll_key();

// Return values for arrow keys (outside normal ASCII range)
constexpr int KEY_UP    = 0x100;
constexpr int KEY_DOWN  = 0x101;
constexpr int KEY_RIGHT = 0x102;
constexpr int KEY_LEFT  = 0x103;

#endif // TERMINAL_IO_H
