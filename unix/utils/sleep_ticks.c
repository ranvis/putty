#include <unistd.h>
#include "platform.h"

void sleep_ticks(int ticks)
{
#if 1000000 % TICKSPERSEC == 0
    usleep(ticks * (1000000 / TICKSPERSEC));
#else
    usleep(ticks * (1000000f / TICKSPERSEC));
#endif
}
