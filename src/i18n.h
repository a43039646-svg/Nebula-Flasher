#ifndef NEBULA_I18N_H
#define NEBULA_I18N_H

void i18n_init(void);
int i18n_set_language(const char *code);
const char *i18n_get(const char *key);
const char *i18n_current_language(void);

#endif
