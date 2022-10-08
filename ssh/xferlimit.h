#ifndef PUTTY_SSH_XFERLIMIT_H
#define PUTTY_SSH_XFERLIMIT_H

void xfer_limit_set(int value);
char *xfer_limit_set_arg(const char *arg);
void xfer_limit_start(void);
void xfer_upload_data_limited(struct fxp_xfer *xfer, char *buf, int len);
void xfer_upload_data_real(struct fxp_xfer *xfer, char *buffer, int len);

#endif
