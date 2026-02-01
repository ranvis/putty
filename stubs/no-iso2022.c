#include <stdlib.h>
#include "putty.h"

int iso2022_init(struct iso2022_data *ctx, const char *initstr, int mode)
{
    debug(__FUNCTION__ " is called while " __FILE__ " is linked.\n");
    return -1;
}

int iso2022_init_test(const char *initstr)
{
    return -1;
}
