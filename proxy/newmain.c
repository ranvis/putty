#include "putty.h"

Socket *new_main_connection(
    SockAddr *addr, const char *hostname, int port, bool privport,
    bool oobinline, bool nodelay, bool keepalive, Plug *plug, Conf *conf,
    Interactor *itr, LogContext *logctx)
{
    /* Execute pre-connection command hook */
    execute_command_hook(logctx, conf_get_str(conf, CONF_pre_connect_command),
                         addr, port, conf);

    /* Now open the socket */
    return new_connection(addr, hostname, port, privport, oobinline, nodelay,
                          keepalive, plug, conf, itr);
}
