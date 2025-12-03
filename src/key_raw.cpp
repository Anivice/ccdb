#include <termios.h>
#include <unistd.h>
#include <cstdlib>

static termios old_tio, new_tio;

void reset_terminal_mode()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
}

void set_conio_terminal_mode()
{
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
    atexit(reset_terminal_mode);
}
