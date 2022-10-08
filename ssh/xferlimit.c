#include "putty.h"
#include "sftp.h"
#include "xferlimit.h"

static unsigned long lim_base_time;
static int lim_xfer_max, lim_up_sent, lim_delay_unit;

void xfer_limit_set(int value)
{
    int unit_per_sec = max(1, min(5, (value / 4096)));
    lim_delay_unit = TICKSPERSEC / unit_per_sec;
    lim_xfer_max = max(0, min(128 << 20, value) / unit_per_sec); // 0..128MiB
}

char *xfer_limit_set_arg(const char *arg)
{
    if (!arg || !*arg || (*arg < '0' || *arg > '9'))
        return l10n_dupstr("-limit requires an argument");
    int value = atoi(arg);
    xfer_limit_set(value * 1000 / 8); // kbit to byte
    return NULL;
}

void xfer_limit_start(void)
{
    lim_base_time = GETTICKCOUNT();
    lim_up_sent = 0;
}

void xfer_upload_data_limited(struct fxp_xfer *xfer, char *buf, int len)
{
    if (!lim_xfer_max) {
        xfer_upload_data_real(xfer, buf, len);
        return;
    }
    while (len > 0) {
        unsigned long now = GETTICKCOUNT();
        unsigned long passed = now - lim_base_time;
        if (lim_up_sent && lim_up_sent >= lim_xfer_max / lim_delay_unit * passed) {
            if (passed >= lim_delay_unit) {
                unsigned int units = passed / lim_delay_unit;
                passed %= lim_delay_unit;
                lim_base_time += units * lim_delay_unit;
                int sent_sub = lim_xfer_max * units;
                lim_up_sent = lim_up_sent > sent_sub ? lim_up_sent - sent_sub : 0;
            }
            int delay = (long long)lim_up_sent * lim_delay_unit / lim_xfer_max - passed;
            sleep_ticks(max(100, min(6 * TICKSPERSEC, delay)));
        }
        int xfer_len = len > lim_xfer_max ? lim_xfer_max : len;
        xfer_upload_data_real(xfer, buf, xfer_len);
        lim_up_sent += xfer_len;
        buf += xfer_len;
        len -= xfer_len;
    }
}
