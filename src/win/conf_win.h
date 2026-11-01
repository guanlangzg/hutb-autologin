#ifndef HUTB_CONF_WIN_H
#define HUTB_CONF_WIN_H

#include <wchar.h>

void conf_init(void);
void conf_set_dir(const wchar_t *dir);
const wchar_t *conf_dat_path(void);
const wchar_t *conf_log_path(void);
void conf_import_legacy_ini(void);

#endif
