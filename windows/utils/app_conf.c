#include "putty.h"
#include "ini.h"

int app_conf_get_int(const char *name, int def, int min, int max)
{
    int value = def;
    if (get_use_inifile()) {
        if (!get_ini_int(appname, name, inifile, &value))
            value = def;
    } else {
        char path[256];
        snprintf(path, sizeof path, PUTTY_REG_POS "\\%s", appname);
        HKEY hkey = open_regkey_ro(HKEY_CURRENT_USER, path);
        if (hkey) {
            DWORD dw;
            if (get_reg_dword(hkey, name, &dw))
                value = dw;
            else
                value = def;
            close_regkey(hkey);
        }
    }
    if (value < min || max < value) {
        value = def;
    }
    return value;
}
