#ifndef PUTTY_WINDOWS_INI_H
#define PUTTY_WINDOWS_INI_H

extern char inifile[2 * MAX_PATH + 10];

int get_use_inifile(void);
char *change_ini_path(const char *new_path);

#endif
