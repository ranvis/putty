// PuTTYrv

#ifdef PUTTY_ACTIVATE_DASSERT
#define dassert(expr) assert(expr)
#else
#define dassert(expr) (void)0
#endif
