#include "putty.h"

void sleep_ticks(int ticks)
{
#if TICKSPERSEC == 1000
    Sleep(ticks);
#else
    Sleep(ticks * (1000f / TICKSPERSEC));
#endif
}
