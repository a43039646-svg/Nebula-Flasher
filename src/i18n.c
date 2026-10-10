#include "i18n.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#ifndef NEBULA_LOCALEDIR
#define NEBULA_LOCALEDIR "/usr/local/share/nebula-flasher/locales"
#endif

static GHashTable *current_table = NULL;
static GHashTable *fallback_table = NULL;
static gchar current_code[16] = "en";
static gchar locale_dir[PATH_MAX];

static GHashTable *load_file(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
        return NULL;

    GHashTable *table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    char line[2048];

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = '\0';

        if (line[0] == '#' || line[0] == '\0')
            continue;

        char *sep = strchr(line, '=');
        if (!sep)
            continue;

        *sep = '\0';
        char *key = g_strstrip(line);
        char *value = g_strstrip(sep + 1);

        if (strcmp(key, "FALLBACK") == 0)
            continue;

        g_hash_table_replace(table, g_strdup(key), g_strdup(value));
    }

    fclose(file);
    return table;
}

static gchar *find_locale_dir(void)
{
    const char *candidates[] = {
        NEBULA_LOCALEDIR,
        "./locales",
        NULL
    };

    for (guint i = 0; candidates[i]; i++) {
        if (g_file_test(candidates[i], G_FILE_TEST_IS_DIR))
            return g_strdup(candidates[i]);
    }

    gchar *cwd = g_get_current_dir();
    gchar *candidate = g_build_filename(cwd, "locales", NULL);
    g_free(cwd);

    if (g_file_test(candidate, G_FILE_TEST_IS_DIR))
        return candidate;

    g_free(candidate);
    return g_strdup(NEBULA_LOCALEDIR);
}

void i18n_init(void)
{
    if (fallback_table)
        g_hash_table_unref(fallback_table);
    if (current_table)
        g_hash_table_unref(current_table);

    g_strlcpy(locale_dir, find_locale_dir(), sizeof(locale_dir));

    gchar *path = g_build_filename(locale_dir, "en.lang", NULL);
    fallback_table = load_file(path);
    g_free(path);

    if (!fallback_table)
        fallback_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    current_table = NULL;
    g_strlcpy(current_code, "en", sizeof(current_code));
}

int i18n_set_language(const char *code)
{
    if (!code || !*code)
        code = "en";

    gchar *path = g_build_filename(locale_dir, code, NULL);
    gchar *file_path = g_strconcat(path, ".lang", NULL);
    g_free(path);

    GHashTable *table = load_file(file_path);
    g_free(file_path);

    if (!table) {
        g_strlcpy(current_code, "en", sizeof(current_code));
        if (current_table) {
            g_hash_table_unref(current_table);
            current_table = NULL;
        }
        return 0;
    }

    if (current_table)
        g_hash_table_unref(current_table);

    current_table = table;
    g_strlcpy(current_code, code, sizeof(current_code));
    return 1;
}

const char *i18n_get(const char *key)
{
    if (!key)
        return "";

    if (current_table) {
        const char *translated = g_hash_table_lookup(current_table, key);
        if (translated)
            return translated;
    }

    const char *fallback = fallback_table
        ? g_hash_table_lookup(fallback_table, key)
        : NULL;

    return fallback ? fallback : key;
}

const char *i18n_current_language(void)
{
    return current_code;
}
