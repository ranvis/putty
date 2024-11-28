#ifndef PUTTY_WINDOWS_INI_H
#define PUTTY_WINDOWS_INI_H

typedef struct ini_sections ini_sections;

extern char inifile[];

typedef void (*sprintf_void_fp)(const char *fmt, ...);

bool get_use_inifile(void);
char *change_ini_path(const char *new_path);
void cmdline_arg_process_ini_option(CmdlineArgList *arglist, size_t *arglistpos, sprintf_void_fp error_cb);
void aux_match_process_ini_option(AuxMatchOpt *amo);

char *create_ini_section(const char *name, const char *prefix, const char *ini_file);
char *open_ini_section(const char *name, const char *prefix, const char *ini_file);
void delete_ini_section(const char *name, const char *prefix, const char *ini_file);

ini_sections *enum_ini_sections(const char *ini_path);
bool enum_ini_section_next(ini_sections *sects, strbuf *sb, const char *prefix);
void enum_ini_section_finish(ini_sections *e);

bool put_ini_sz(const char *section, const char *name, const char *value, const char *ini_file);
char *get_ini_sz(const char *section, const char *name, const char *ini_file);
bool put_ini_int(const char *section, const char *name, int value, const char *ini_file);
bool get_ini_int(const char *section, const char *name, const char *ini_file, int *value);
strbuf *get_ini_multi_sz(const char *section, const char *name, const char *ini_file);
bool put_ini_multi_sz(const char *section, const char *name, strbuf *value, const char *ini_file);

#endif
