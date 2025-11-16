#include "tui.h"
volatile std::atomic_bool resized = false;
void _sighandler(int)
{
    resized = true;
}
