#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include "i18n.h"

#define APP_ID "io.nebula.Flasher"
#define WRITER_PATH "/usr/libexec/nebula-flasher-writer"
#define APP_VERSION "1.0.2"

typedef struct {
    char language[8];
    char theme[16];
    char shape[16];
    guint refresh_seconds;
} UiConfig;

static UiConfig ui_config = {"en", "auto", "soft", 1};
static GtkCssProvider *global_css_provider = NULL;
static gboolean global_css_provider_installed = FALSE;

static const char *tr(const char *key) { return i18n_get(key); }
static void apply_css(void);

typedef struct {
    char *path;
    char *model;
    char *transport;
    char *major_minor;
    guint64 size;
    char *label;
} Drive;

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *image_label;
    GtkWidget *image_button;
    GtkWidget *device_dropdown;
    GtkWidget *refresh_button;
    GtkWidget *confirm_check;
    GtkWidget *flash_button;
    GtkWidget *progress;
    GtkWidget *status;
    GtkWidget *content_stack;
    GtkWidget *settings_button;
    GtkWidget *settings_status;
    GtkWidget *refresh_dropdown;
    GtkWidget *theme_dropdown;
    GtkWidget *shape_dropdown;
    GtkWidget *language_dropdown;
    GtkStringList *device_model;
    GPtrArray *drives;
    char *image_path;
    GSubprocess *process;
    GDataInputStream *output;
    gboolean done_message_seen;
    guint64 image_size;
    gint64 flash_started_usec;
    char *last_error;
    guint refresh_source;
} AppState;

static void detect_system_language(void) {
    const char *lang = getenv("LANG");
    if (!lang || !*lang) return;
    if (g_str_has_prefix(lang, "C") || g_str_has_prefix(lang, "POSIX")) {
        g_strlcpy(ui_config.language, "en", sizeof ui_config.language);
        return;
    }
    char code[8] = {0};
    size_t len = 0;
    while (lang[len] && lang[len] != '_' && lang[len] != '-' && lang[len] != '.' && len < sizeof code - 1)
        len++;
    if (len > 0) {
        memcpy(code, lang, len);
        code[len] = '\0';
        g_strlcpy(ui_config.language, code, sizeof ui_config.language);
    }
}

static void load_ui_config(void) {
    detect_system_language();
    GKeyFile *key = g_key_file_new();
    GError *error = NULL;
    char *path = g_build_filename(g_get_user_config_dir(), "nebula-flasher", "config.ini", NULL);
    gboolean own_config_loaded = g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, &error);
    if (own_config_loaded) {
        char *lang = g_key_file_get_string(key, "ui", "language", NULL);
        char *theme = g_key_file_get_string(key, "ui", "theme", NULL);
        char *shape = g_key_file_get_string(key, "ui", "shape", NULL);
        gint refresh = g_key_file_get_integer(key, "ui", "refresh_seconds", NULL);
        if (lang && *lang) g_strlcpy(ui_config.language, lang, sizeof ui_config.language);
        if (theme && *theme) g_strlcpy(ui_config.theme, theme, sizeof ui_config.theme);
        if (shape && *shape) g_strlcpy(ui_config.shape, shape, sizeof ui_config.shape);
        if (refresh == 1 || refresh == 2 || refresh == 5) ui_config.refresh_seconds = (guint)refresh;
        g_free(lang); g_free(theme); g_free(shape);
    }
    g_clear_error(&error);
    g_key_file_unref(key);
    g_free(path);

    /* Match the existing Control Center preferences on first launch. */
    if (!own_config_loaded) {
        GKeyFile *ncc = g_key_file_new();
        char *ncc_path = g_build_filename(g_get_user_config_dir(), "nebula-control-center", "config.ini", NULL);
        if (g_key_file_load_from_file(ncc, ncc_path, G_KEY_FILE_NONE, NULL)) {
            char *lang = g_key_file_get_string(ncc, "ui", "language", NULL);
            char *theme = g_key_file_get_string(ncc, "ui", "theme", NULL);
            char *shape = g_key_file_get_string(ncc, "ui", "shape", NULL);
            gint refresh = g_key_file_get_integer(ncc, "ui", "refresh_seconds", NULL);
            if (lang && *lang) g_strlcpy(ui_config.language, lang, sizeof ui_config.language);
            if (theme && *theme) g_strlcpy(ui_config.theme, theme, sizeof ui_config.theme);
            if (shape && *shape) g_strlcpy(ui_config.shape, shape, sizeof ui_config.shape);
            if (refresh == 1 || refresh == 2 || refresh == 5) ui_config.refresh_seconds = (guint)refresh;
            g_free(lang); g_free(theme); g_free(shape);
        }
        g_key_file_unref(ncc);
        g_free(ncc_path);
    }

    i18n_init();
    if (!i18n_set_language(ui_config.language)) {
        g_strlcpy(ui_config.language, "en", sizeof ui_config.language);
        i18n_set_language("en");
    }
}

static void save_ui_config(void) {
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "ui", "language", ui_config.language);
    g_key_file_set_string(key, "ui", "theme", ui_config.theme);
    g_key_file_set_string(key, "ui", "shape", ui_config.shape);
    g_key_file_set_integer(key, "ui", "refresh_seconds", (gint)ui_config.refresh_seconds);
    gsize length = 0;
    GError *error = NULL;
    char *data = g_key_file_to_data(key, &length, &error);
    if (data) {
        char *dir = g_build_filename(g_get_user_config_dir(), "nebula-flasher", NULL);
        if (g_mkdir_with_parents(dir, 0700) == 0) {
            char *config_path = g_build_filename(dir, "config.ini", NULL);
            g_file_set_contents(config_path, data, (gssize)length, NULL);
            g_free(config_path);
        }
        g_free(dir);
        g_free(data);
    }
    g_clear_error(&error);
    g_key_file_unref(key);
}

static const char *effective_theme(void) {
    if (strcmp(ui_config.theme, "auto") != 0) return ui_config.theme;
    FILE *file = fopen("/etc/os-release", "r");
    if (!file) return "nebula";
    char line[256];
    static char detected[16] = "nebula";
    g_strlcpy(detected, "nebula", sizeof detected);
    while (fgets(line, sizeof line, file)) {
        if (strncmp(line, "ID=", 3) != 0) continue;
        char *value = line + 3;
        value[strcspn(value, "\r\n\"")] = '\0';
        if (strstr(value, "mint")) g_strlcpy(detected, "mint", sizeof detected);
        else if (strstr(value, "ubuntu")) g_strlcpy(detected, "ubuntu", sizeof detected);
        else if (strstr(value, "arch")) g_strlcpy(detected, "arch", sizeof detected);
        else if (strstr(value, "fedora")) g_strlcpy(detected, "fedora", sizeof detected);
        else if (strstr(value, "debian")) g_strlcpy(detected, "debian", sizeof detected);
        else if (strstr(value, "manjaro")) g_strlcpy(detected, "manjaro", sizeof detected);
        else if (strstr(value, "opensuse") || strstr(value, "suse")) g_strlcpy(detected, "opensuse", sizeof detected);
        else if (strstr(value, "pop")) g_strlcpy(detected, "pop", sizeof detected);
        else if (strstr(value, "elementary")) g_strlcpy(detected, "elementary", sizeof detected);
        else if (strstr(value, "zorin")) g_strlcpy(detected, "zorin", sizeof detected);
        else if (strstr(value, "kali")) g_strlcpy(detected, "kali", sizeof detected);
        break;
    }
    fclose(file);
    return detected;
}

static const char *theme_primary(void) {
    const char *theme = effective_theme();
    if (!strcmp(theme, "mint")) return "#8cc63f";
    if (!strcmp(theme, "ubuntu")) return "#e95420";
    if (!strcmp(theme, "arch")) return "#1793d1";
    if (!strcmp(theme, "fedora")) return "#51a2da";
    if (!strcmp(theme, "debian")) return "#d70a53";
    if (!strcmp(theme, "manjaro")) return "#35bf5c";
    if (!strcmp(theme, "opensuse")) return "#73ba25";
    if (!strcmp(theme, "pop")) return "#48b9c7";
    if (!strcmp(theme, "elementary")) return "#64b9ef";
    if (!strcmp(theme, "zorin")) return "#0b7fab";
    if (!strcmp(theme, "kali")) return "#557c94";
    if (!strcmp(theme, "ocean")) return "#2bb7d9";
    if (!strcmp(theme, "rose")) return "#e86ba5";
    if (!strcmp(theme, "amber")) return "#f0a202";
    return "#8f73ff";
}

static const char *shape_radius(void) {
    if (!strcmp(ui_config.shape, "square")) return "2px";
    if (!strcmp(ui_config.shape, "soft")) return "8px";
    return "16px";
}

static void drive_free(gpointer data) {
    Drive *drive = data;
    if (!drive) return;
    g_free(drive->path);
    g_free(drive->model);
    g_free(drive->transport);
    g_free(drive->major_minor);
    g_free(drive->label);
    g_free(drive);
}

static char *format_device_label(const char *model, guint64 size, const char *path) {
    char *pretty_size = g_format_size(size);
    const char *pretty_model = (model && *model) ? model : "USB / removable drive";
    char *label = g_strdup_printf("%s  ·  %s  ·  %s", pretty_model, pretty_size, path);
    g_free(pretty_size);
    return label;
}

static GHashTable *parse_lsblk_line(const char *line) {
    GHashTable *values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gint argc = 0;
    gchar **argv = NULL;
    if (!g_shell_parse_argv(line, &argc, &argv, NULL)) return values;
    for (gint i = 0; i < argc; i++) {
        gchar **parts = g_strsplit(argv[i], "=", 2);
        if (parts[0] && parts[1])
            g_hash_table_replace(values, g_strdup(parts[0]), g_strdup(parts[1]));
        g_strfreev(parts);
    }
    g_strfreev(argv);
    return values;
}

static void set_status(AppState *state, const char *message) {
    gtk_label_set_text(GTK_LABEL(state->status), message);
}

static void update_flash_button(AppState *state) {
    gboolean image_ok = state->image_path && *state->image_path;
    gboolean device_ok = state->drives && state->drives->len > 0 &&
        gtk_drop_down_get_selected(GTK_DROP_DOWN(state->device_dropdown)) != GTK_INVALID_LIST_POSITION;
    gboolean confirmed = gtk_check_button_get_active(GTK_CHECK_BUTTON(state->confirm_check));
    gboolean running = state->process != NULL;
    gtk_widget_set_sensitive(state->flash_button, image_ok && device_ok && confirmed && !running);
}

static gboolean drive_arrays_equal(GPtrArray *left, GPtrArray *right) {
    if (!left || !right || left->len != right->len) return FALSE;
    for (guint i = 0; i < left->len; i++) {
        Drive *a = g_ptr_array_index(left, i);
        Drive *b = g_ptr_array_index(right, i);
        if (g_strcmp0(a->path, b->path) != 0 ||
            g_strcmp0(a->model, b->model) != 0 ||
            g_strcmp0(a->transport, b->transport) != 0 ||
            g_strcmp0(a->major_minor, b->major_minor) != 0 ||
            a->size != b->size)
            return FALSE;
    }
    return TRUE;
}

static void refresh_devices(AppState *state, gboolean quiet) {
    if (state->process) return;

    char *previous_path = NULL;
    char *previous_major_minor = NULL;
    guint previous_index = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->device_dropdown));
    if (previous_index != GTK_INVALID_LIST_POSITION && previous_index < state->drives->len) {
        Drive *previous = g_ptr_array_index(state->drives, previous_index);
        previous_path = g_strdup(previous->path);
        previous_major_minor = g_strdup(previous->major_minor);
    }
    gboolean had_devices = state->drives->len > 0;

    const char *argv[] = {
        "lsblk", "-P", "-b", "-o", "NAME,PATH,PKNAME,MAJ:MIN,TYPE,RM,TRAN,SIZE,MODEL,MOUNTPOINT", NULL
    };
    gchar *stdout_text = NULL;
    gchar *stderr_text = NULL;
    gint exit_status = 0;
    GError *error = NULL;
    gboolean spawned = g_spawn_sync(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, &stdout_text, &stderr_text, &exit_status, &error);

    if (!spawned || exit_status != 0 || !stdout_text) {
        const char *detail = error ? error->message : (stderr_text ? stderr_text : "");
        char *message = g_strdup_printf("%s %s", tr("Could not list drives. Install util-linux (lsblk)."), detail);
        if (had_devices) {
            g_ptr_array_set_size(state->drives, 0);
            g_object_unref(state->device_model);
            state->device_model = gtk_string_list_new(NULL);
            gtk_drop_down_set_model(GTK_DROP_DOWN(state->device_dropdown), G_LIST_MODEL(state->device_model));
            gtk_drop_down_set_selected(GTK_DROP_DOWN(state->device_dropdown), GTK_INVALID_LIST_POSITION);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(state->confirm_check), FALSE);
        }
        if (!quiet || had_devices) set_status(state, message);
        g_free(message);
        g_clear_error(&error);
        g_free(stdout_text);
        g_free(stderr_text);
        g_free(previous_path);
        g_free(previous_major_minor);
        update_flash_button(state);
        return;
    }

    GPtrArray *new_drives = g_ptr_array_new_with_free_func(drive_free);
    GtkStringList *new_model = gtk_string_list_new(NULL);
    gchar **lines = g_strsplit(stdout_text, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        if (!*lines[i]) continue;
        GHashTable *fields = parse_lsblk_line(lines[i]);
        const char *type = g_hash_table_lookup(fields, "TYPE");
        const char *major_minor = g_hash_table_lookup(fields, "MAJ:MIN");
        const char *path = g_hash_table_lookup(fields, "PATH");
        const char *rm = g_hash_table_lookup(fields, "RM");
        const char *tran = g_hash_table_lookup(fields, "TRAN");
        const char *size_text = g_hash_table_lookup(fields, "SIZE");
        const char *model = g_hash_table_lookup(fields, "MODEL");

        if (type && !strcmp(type, "disk") && path && !strncmp(path, "/dev/", 5) &&
            major_minor && *major_minor &&
            ((rm && !strcmp(rm, "1")) || (tran && !strcmp(tran, "usb")))) {
            Drive *drive = g_new0(Drive, 1);
            drive->path = g_strdup(path);
            drive->model = g_strdup(model ? model : "");
            drive->transport = g_strdup(tran ? tran : "");
            drive->major_minor = g_strdup(major_minor);
            drive->size = size_text ? g_ascii_strtoull(size_text, NULL, 10) : 0;
            drive->label = format_device_label(drive->model, drive->size, drive->path);
            gtk_string_list_append(new_model, drive->label);
            g_ptr_array_add(new_drives, drive);
        }
        g_hash_table_unref(fields);
    }
    g_strfreev(lines);
    g_free(stdout_text);
    g_free(stderr_text);

    /* Do not reset the dropdown every second when the USB list is unchanged. */
    if (drive_arrays_equal(state->drives, new_drives)) {
        g_ptr_array_unref(new_drives);
        g_object_unref(new_model);
        if (!quiet) {
            if (state->drives->len)
                set_status(state, tr("Ready. Select an image, choose a drive, and confirm data erasure."));
            else
                set_status(state, tr("No removable USB drives detected. Connect a USB drive and refresh."));
        }
        g_free(previous_path);
        g_free(previous_major_minor);
        update_flash_button(state);
        return;
    }

    g_ptr_array_unref(state->drives);
    state->drives = new_drives;
    g_object_unref(state->device_model);
    state->device_model = new_model;
    gtk_drop_down_set_model(GTK_DROP_DOWN(state->device_dropdown), G_LIST_MODEL(state->device_model));

    guint new_selected = state->drives->len ? 0 : GTK_INVALID_LIST_POSITION;
    gboolean selection_lost = FALSE;
    if (previous_path) {
        new_selected = GTK_INVALID_LIST_POSITION;
        for (guint i = 0; i < state->drives->len; i++) {
            Drive *drive = g_ptr_array_index(state->drives, i);
            if (g_strcmp0(drive->path, previous_path) == 0 &&
                g_strcmp0(drive->major_minor, previous_major_minor) == 0) {
                new_selected = i;
                break;
            }
        }
        selection_lost = (new_selected == GTK_INVALID_LIST_POSITION);
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->device_dropdown), new_selected);
    if (selection_lost) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(state->confirm_check), FALSE);
        set_status(state, tr("The selected USB drive changed or disappeared. Choose a drive again and reconfirm."));
    } else if (!quiet) {
        if (state->drives->len)
            set_status(state, tr("Ready. Select an image, choose a drive, and confirm data erasure."));
        else
            set_status(state, tr("No removable USB drives detected. Connect a USB drive and refresh."));
    } else if (had_devices && !state->drives->len) {
        set_status(state, tr("No removable USB drives detected. Connect a USB drive and refresh."));
    }
    g_free(previous_path);
    g_free(previous_major_minor);
    update_flash_button(state);
}

static void on_device_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object; (void)pspec;
    update_flash_button(user_data);
}

static void on_confirmation_toggled(GtkCheckButton *button, gpointer user_data) {
    (void)button;
    update_flash_button(user_data);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    refresh_devices(user_data, FALSE);
}

static gboolean periodic_refresh(gpointer user_data) {
    AppState *state = user_data;
    if (!state->process) refresh_devices(state, TRUE);
    return G_SOURCE_CONTINUE;
}

static void restart_refresh_timer(AppState *state) {
    if (state->refresh_source) {
        g_source_remove(state->refresh_source);
        state->refresh_source = 0;
    }
    state->refresh_source = g_timeout_add_seconds(ui_config.refresh_seconds, periodic_refresh, state);
}

static void on_file_chosen(GtkNativeDialog *dialog, gint response, gpointer user_data) {
    AppState *state = user_data;
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        if (file) {
            char *path = g_file_get_path(file);
            if (path) {
                g_free(state->image_path);
                state->image_path = path;
                char *base = g_path_get_basename(path);
                GStatBuf stat_buf;
                if (g_stat(path, &stat_buf) == 0 && stat_buf.st_size >= 0) {
                    state->image_size = (guint64)stat_buf.st_size;
                    char *size = g_format_size((guint64)stat_buf.st_size);
                    char *label = g_strdup_printf("%s  ·  %s", base, size);
                    gtk_label_set_text(GTK_LABEL(state->image_label), label);
                    g_free(size);
                    g_free(label);
                } else {
                    state->image_size = 0;
                    gtk_label_set_text(GTK_LABEL(state->image_label), base);
                }
                g_free(base);
                set_status(state, tr("Image selected. Double-check the target drive before flashing."));
                update_flash_button(state);
            }
            g_object_unref(file);
        }
    }
    g_object_unref(dialog);
}

static void on_choose_image_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = user_data;
    GtkFileChooserNative *chooser = gtk_file_chooser_native_new(
        tr("Choose OS image"), GTK_WINDOW(state->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        tr("Open image"), tr("Cancel"));
    GtkFileFilter *images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, "Disk images (*.iso, *.img, *.raw, *.bin)");
    gtk_file_filter_add_pattern(images, "*.iso");
    gtk_file_filter_add_pattern(images, "*.ISO");
    gtk_file_filter_add_pattern(images, "*.img");
    gtk_file_filter_add_pattern(images, "*.IMG");
    gtk_file_filter_add_pattern(images, "*.raw");
    gtk_file_filter_add_pattern(images, "*.bin");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), images);
    g_signal_connect(chooser, "response", G_CALLBACK(on_file_chosen), state);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
}

static void flash_read_next_line(AppState *state);

static char *format_eta(guint64 seconds) {
    guint64 minutes = seconds / 60;
    guint64 remainder = seconds % 60;
    return g_strdup_printf("%02" G_GUINT64_FORMAT ":%02" G_GUINT64_FORMAT, minutes, remainder);
}

static void on_process_waited(GObject *source, GAsyncResult *result, gpointer user_data) {
    AppState *state = user_data;
    GError *error = NULL;
    gboolean ok = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error);
    if (ok && state->done_message_seen) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress), 1.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress), "100%");
        set_status(state, tr("Flash and verification completed successfully. You can safely remove the drive."));
    } else {
        const char *detail = state->last_error ? state->last_error :
            (error ? error->message : tr("Check the error above and try again."));
        char *message = g_strdup_printf(tr("Flash failed or was cancelled. %s"), detail);
        set_status(state, message);
        g_free(message);
    }
    g_clear_pointer(&state->last_error, g_free);
    g_clear_error(&error);
    g_clear_object(&state->output);
    g_clear_object(&state->process);
    gtk_widget_set_sensitive(state->image_button, TRUE);
    gtk_widget_set_sensitive(state->refresh_button, TRUE);
    gtk_widget_set_sensitive(state->device_dropdown, TRUE);
    gtk_widget_set_sensitive(state->confirm_check, TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(state->confirm_check), FALSE);
    update_flash_button(state);
}

static void on_output_line(GObject *source, GAsyncResult *result, gpointer user_data) {
    AppState *state = user_data;
    GError *error = NULL;
    gsize length = 0;
    char *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), result, &length, &error);
    if (line) {
        if (g_str_has_prefix(line, "PROGRESS ")) {
            guint percent = (guint)g_ascii_strtoull(line + 9, NULL, 10);
            double fraction = ((double)percent / 100.0) * 0.80;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress), fraction);
            char *text = NULL;
            gint64 elapsed_usec = g_get_monotonic_time() - state->flash_started_usec;
            if (percent > 0 && state->image_size > 0 && elapsed_usec > 0) {
                double elapsed_seconds = (double)elapsed_usec / 1000000.0;
                double bytes_per_second = ((double)state->image_size * ((double)percent / 100.0)) / elapsed_seconds;
                guint64 remaining_seconds = (guint64)(elapsed_seconds * (100.0 - (double)percent) / (double)percent);
                char *rate = g_format_size((guint64)bytes_per_second);
                char *eta = format_eta(remaining_seconds);
                text = g_strdup_printf(tr("Writing: %u%% · %s/s · %s remaining"), percent, rate, eta);
                g_free(rate);
                g_free(eta);
            } else {
                text = g_strdup_printf(tr("Writing: %u%%"), percent);
            }
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress), text);
            set_status(state, text);
            g_free(text);
        } else if (g_str_has_prefix(line, "VERIFY ")) {
            guint percent = (guint)g_ascii_strtoull(line + 7, NULL, 10);
            double fraction = 0.80 + ((double)percent / 100.0) * 0.20;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress), fraction);
            char *text = g_strdup_printf(tr("Verifying: %u%%"), percent);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress), text);
            set_status(state, text);
            g_free(text);
        } else if (!strcmp(line, "DONE")) {
            state->done_message_seen = TRUE;
        } else if (g_str_has_prefix(line, "ERROR:")) {
            g_free(state->last_error);
            state->last_error = g_strdup(line);
            set_status(state, line);
        } else if (*line && !g_str_has_prefix(line, "====")) {
            set_status(state, line);
        }
        g_free(line);
        flash_read_next_line(state);
    } else {
        g_clear_error(&error);
        g_subprocess_wait_check_async(state->process, NULL, on_process_waited, state);
    }
}

static void flash_read_next_line(AppState *state) {
    g_data_input_stream_read_line_async(state->output, G_PRIORITY_DEFAULT, NULL, on_output_line, state);
}

static void start_flash(AppState *state) {
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->device_dropdown));
    if (!state->image_path || selected == GTK_INVALID_LIST_POSITION || selected >= state->drives->len) return;
    Drive *drive = g_ptr_array_index(state->drives, selected);

    const char *argv[] = { "pkexec", WRITER_PATH, state->image_path, drive->path, drive->major_minor, NULL };
    GError *error = NULL;
    state->process = g_subprocess_newv(argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE, &error);
    if (!state->process) {
        char *message = g_strdup_printf(tr("Could not start the privileged writer: %s"), error->message);
        set_status(state, message);
        g_free(message);
        g_clear_error(&error);
        update_flash_button(state);
        return;
    }

    state->done_message_seen = FALSE;
    g_clear_pointer(&state->last_error, g_free);
    state->flash_started_usec = g_get_monotonic_time();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress), tr("Starting…"));
    set_status(state, tr("Requesting permission to write to the USB drive…"));
    gtk_widget_set_sensitive(state->image_button, FALSE);
    gtk_widget_set_sensitive(state->refresh_button, FALSE);
    gtk_widget_set_sensitive(state->device_dropdown, FALSE);
    gtk_widget_set_sensitive(state->confirm_check, FALSE);
    update_flash_button(state);
    state->output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(state->process));
    flash_read_next_line(state);
}

static void on_confirm_flash_response(GtkDialog *dialog, gint response, gpointer user_data) {
    AppState *state = user_data;
    gtk_window_destroy(GTK_WINDOW(dialog));
    if (response == GTK_RESPONSE_ACCEPT) start_flash(state);
}

static void on_flash_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->device_dropdown));
    if (selected == GTK_INVALID_LIST_POSITION || selected >= state->drives->len || !state->image_path) return;
    Drive *drive = g_ptr_array_index(state->drives, selected);
    char *base = g_path_get_basename(state->image_path);
    char *size = g_format_size(drive->size);
    char *text = g_strdup_printf(
        "%s\n\n%s\n%s · %s\n\n%s %s\n\n%s",
        tr("This will permanently erase all data on:"), drive->path,
        drive->model && *drive->model ? drive->model : tr("Removable drive"), size,
        tr("Image:"), base, tr("Do not continue unless this is the correct USB drive."));
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(state->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", tr("Erase and flash this drive?"));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", text);
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), tr("Cancel"), GTK_RESPONSE_CANCEL,
        tr("Erase and flash"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    g_signal_connect(dialog, "response", G_CALLBACK(on_confirm_flash_response), state);
    gtk_window_present(GTK_WINDOW(dialog));
    g_free(base);
    g_free(size);
    g_free(text);
}

static GtkWidget *section_title(const char *number, const char *title) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *badge = gtk_label_new(number);
    gtk_widget_add_css_class(badge, "step-number");
    GtkWidget *label = gtk_label_new(tr(title));
    gtk_widget_add_css_class(label, "section-title");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row), badge);
    gtk_box_append(GTK_BOX(row), label);
    return row;
}

static GtkWidget *setting_row(const char *label_text, GtkWidget *control) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(row, "settings-row");
    GtkWidget *label = gtk_label_new(tr(label_text));
    gtk_widget_add_css_class(label, "settings-row-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), control);
    return row;
}

static const char *theme_codes[] = {"auto", "nebula", "mint", "ubuntu", "arch", "fedora", "debian", "manjaro", "opensuse", "pop", "elementary", "zorin", "kali", "ocean", "rose", "amber"};
static const char *shape_codes[] = {"rounded", "soft", "square"};
static const char *language_codes[] = {"en", "uk", "ru", "da", "de", "fr", "es", "it", "pt", "nl", "pl", "cs", "sk", "sl", "hr", "sr", "bg", "ro", "hu", "el", "tr", "sv", "no", "fi", "et", "lv", "lt", "is", "ga", "mt", "sq", "mk", "bs", "ca", "eu", "gl", "cy", "eo", "hi", "bn", "ur", "pa", "gu", "mr", "ta", "te", "kn", "ml", "si", "ne", "th", "vi", "id", "ms", "fil", "sw", "am", "af", "zu", "xh", "yo", "ig", "ha", "ar", "he", "fa", "ps", "kk", "uz", "ky", "tg", "tk", "az", "hy", "ka", "mn", "ja", "ko", "zh", "la", "jv", "so", "mg", "ny", "rw", "ht", "lb", "mi", "sm", "to", "fj", "ceb", "co", "haw", "su", "fy", "gd", "oc", "br", "ku", "ba", "tt", "ug", "be"};
static const char *language_names[] = {
    "English", "Українська", "Русский", "Dansk", "Deutsch", "Français", "Español", "Italiano", "Português", "Nederlands", "Polski", "Čeština", "Slovenčina", "Slovenščina", "Hrvatski", "Српски", "Български", "Română", "Magyar", "Ελληνικά", "Türkçe", "Svenska", "Norsk", "Suomi", "Eesti", "Latviešu", "Lietuvių", "Íslenska", "Gaeilge", "Malti", "Shqip", "Македонски", "Bosanski", "Català", "Euskara", "Galego", "Cymraeg", "Esperanto", "हिन्दी", "বাংলা", "اردو", "ਪੰਜਾਬੀ", "ગુજરાતી", "मराठी", "தமிழ்", "తెలుగు", "ಕನ್ನಡ", "മലയാളം", "සිංහල", "नेपाली", "ไทย", "Tiếng Việt", "Bahasa Indonesia", "Bahasa Melayu", "Filipino", "Kiswahili", "አማርኛ", "Afrikaans", "isiZulu", "isiXhosa", "Yorùbá", "Igbo", "Hausa", "العربية", "עברית", "فارسی", "پښتو", "Қазақша", "O‘zbek", "Кыргызча", "Тоҷикӣ", "Türkmençe", "Azərbaycanca", "Հայերեն", "ქართული", "Монгол", "日本語", "한국어", "简体中文", "Latina", "Basa Jawa", "Soomaali", "Malagasy", "Chichewa", "Kinyarwanda", "Kreyòl ayisyen", "Lëtzebuergesch", "Māori", "Gagana Samoa", "Tongan", "Fijian", "Cebuano", "Corsu", "ʻŌlelo Hawaiʻi", "Basa Sunda", "Frysk", "Gàidhlig", "Occitan", "Brezhoneg", "Kurdî", "Башҡортса", "Татарча", "ئۇيغۇرچە", "Беларуская", NULL
};

static guint value_index(const char *value, const char *const *items, guint count, guint fallback) {
    for (guint i = 0; i < count; i++) if (g_strcmp0(value, items[i]) == 0) return i;
    return fallback;
}

static void refresh_setting_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    static const guint intervals[] = {1, 2, 5};
    if (selected < G_N_ELEMENTS(intervals)) {
        ui_config.refresh_seconds = intervals[selected];
        save_ui_config();
        restart_refresh_timer(state);
        gtk_label_set_text(GTK_LABEL(state->settings_status), tr("Settings saved."));
    }
}

static void theme_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected < G_N_ELEMENTS(theme_codes)) {
        g_strlcpy(ui_config.theme, theme_codes[selected], sizeof ui_config.theme);
        save_ui_config();
        apply_css();
        gtk_label_set_text(GTK_LABEL(state->settings_status), tr("Theme applied."));
    }
}

static void shape_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected < G_N_ELEMENTS(shape_codes)) {
        g_strlcpy(ui_config.shape, shape_codes[selected], sizeof ui_config.shape);
        save_ui_config();
        apply_css();
        gtk_label_set_text(GTK_LABEL(state->settings_status), tr("Shape applied."));
    }
}

static void language_changed(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected < G_N_ELEMENTS(language_codes)) {
        g_strlcpy(ui_config.language, language_codes[selected], sizeof ui_config.language);
        save_ui_config();
        i18n_set_language(ui_config.language);
        gtk_label_set_text(GTK_LABEL(state->settings_status), tr("Restart required to apply language."));
    }
}

static void toggle_settings(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = user_data;
    const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(state->content_stack));
    if (g_strcmp0(visible, "settings") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(state->content_stack), "home");
        gtk_button_set_label(GTK_BUTTON(state->settings_button), tr("Settings"));
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(state->content_stack), "settings");
        gtk_button_set_label(GTK_BUTTON(state->settings_button), tr("Back"));
    }
}

static GtkWidget *build_home_page(AppState *state) {
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(content, 28);
    gtk_widget_set_margin_end(content, 28);
    gtk_widget_set_margin_top(content, 2);
    gtk_widget_set_margin_bottom(content, 24);
    gtk_widget_set_vexpand(content, TRUE);

    GtkWidget *image_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(image_card, "step-card");
    gtk_box_append(GTK_BOX(image_card), section_title("01", "Select an image"));
    GtkWidget *image_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    state->image_button = gtk_button_new_with_label(tr("Choose image…"));
    gtk_widget_add_css_class(state->image_button, "secondary-button");
    state->image_label = gtk_label_new(tr("No image selected"));
    gtk_widget_add_css_class(state->image_label, "muted-label");
    gtk_label_set_ellipsize(GTK_LABEL(state->image_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(state->image_label, TRUE);
    gtk_widget_set_halign(state->image_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(image_row), state->image_button);
    gtk_box_append(GTK_BOX(image_row), state->image_label);
    gtk_box_append(GTK_BOX(image_card), image_row);
    gtk_box_append(GTK_BOX(content), image_card);

    GtkWidget *drive_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(drive_card, "step-card");
    gtk_box_append(GTK_BOX(drive_card), section_title("02", "Choose a USB drive"));
    GtkWidget *drive_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    state->device_dropdown = gtk_drop_down_new(NULL, NULL);
    gtk_drop_down_set_model(GTK_DROP_DOWN(state->device_dropdown), G_LIST_MODEL(state->device_model));
    gtk_widget_set_hexpand(state->device_dropdown, TRUE);
    state->refresh_button = gtk_button_new_with_label(tr("Refresh"));
    gtk_widget_add_css_class(state->refresh_button, "secondary-button");
    gtk_box_append(GTK_BOX(drive_row), state->device_dropdown);
    gtk_box_append(GTK_BOX(drive_row), state->refresh_button);
    gtk_box_append(GTK_BOX(drive_card), drive_row);
    GtkWidget *drive_hint = gtk_label_new(tr("Only removable and USB-connected whole disks are listed."));
    gtk_widget_add_css_class(drive_hint, "muted-label");
    gtk_widget_set_halign(drive_hint, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(drive_hint), TRUE);
    gtk_box_append(GTK_BOX(drive_card), drive_hint);
    gtk_box_append(GTK_BOX(content), drive_card);

    GtkWidget *write_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(write_card, "step-card");
    gtk_box_append(GTK_BOX(write_card), section_title("03", "Write and verify"));
    state->confirm_check = gtk_check_button_new_with_label(tr("I understand that all data on the selected drive will be erased."));
    gtk_widget_add_css_class(state->confirm_check, "confirm-check");
    gtk_box_append(GTK_BOX(write_card), state->confirm_check);
    state->flash_button = gtk_button_new_with_label(tr("Flash image"));
    gtk_widget_add_css_class(state->flash_button, "primary-button");
    gtk_widget_set_halign(state->flash_button, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(write_card), state->flash_button);
    state->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->progress), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state->progress), tr("Waiting"));
    gtk_box_append(GTK_BOX(write_card), state->progress);
    state->status = gtk_label_new(tr("Connect a USB drive to get started."));
    gtk_label_set_wrap(GTK_LABEL(state->status), TRUE);
    gtk_widget_set_halign(state->status, GTK_ALIGN_START);
    gtk_widget_add_css_class(state->status, "status-label");
    gtk_box_append(GTK_BOX(write_card), state->status);
    gtk_box_append(GTK_BOX(content), write_card);

    GtkWidget *footer = gtk_label_new(NULL);
    char *footer_text = g_strdup_printf("%s · v%s · %s", tr("Nebula Flasher"), APP_VERSION, tr("Writing destroys data on the selected drive."));
    gtk_label_set_text(GTK_LABEL(footer), footer_text);
    g_free(footer_text);
    gtk_widget_add_css_class(footer, "footer-label");
    gtk_label_set_wrap(GTK_LABEL(footer), TRUE);
    gtk_widget_set_margin_top(footer, 4);
    gtk_box_append(GTK_BOX(content), footer);

    g_signal_connect(state->image_button, "clicked", G_CALLBACK(on_choose_image_clicked), state);
    g_signal_connect(state->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), state);
    g_signal_connect(state->device_dropdown, "notify::selected", G_CALLBACK(on_device_changed), state);
    g_signal_connect(state->confirm_check, "toggled", G_CALLBACK(on_confirmation_toggled), state);
    g_signal_connect(state->flash_button, "clicked", G_CALLBACK(on_flash_clicked), state);
    return content;
}

static GtkWidget *build_settings_page(AppState *state) {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(page, 28);
    gtk_widget_set_margin_end(page, 28);
    gtk_widget_set_margin_top(page, 8);
    gtk_widget_set_margin_bottom(page, 24);

    GtkWidget *title = gtk_label_new(tr("Settings"));
    gtk_widget_add_css_class(title, "settings-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    GtkWidget *subtitle = gtk_label_new(tr("Preferences for Nebula Flasher."));
    gtk_widget_add_css_class(subtitle, "muted-label");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), title);
    gtk_box_append(GTK_BOX(page), subtitle);

    const char *refresh_labels[] = {tr("1 second"), tr("2 seconds"), tr("5 seconds"), NULL};
    state->refresh_dropdown = gtk_drop_down_new_from_strings(refresh_labels);
    guint intervals[] = {1, 2, 5};
    guint refresh_selected = 0;
    for (guint i = 0; i < G_N_ELEMENTS(intervals); i++) if (ui_config.refresh_seconds == intervals[i]) refresh_selected = i;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->refresh_dropdown), refresh_selected);
    gtk_box_append(GTK_BOX(page), setting_row("Refresh interval", state->refresh_dropdown));
    g_signal_connect(state->refresh_dropdown, "notify::selected", G_CALLBACK(refresh_setting_changed), state);

    const char *theme_labels[] = {tr("Auto"), tr("Nebula"), tr("Mint"), tr("Ubuntu"), tr("Arch"), tr("Fedora"), tr("Debian"), tr("Manjaro"), tr("openSUSE"), tr("Pop!_OS"), tr("elementary"), tr("Zorin"), tr("Kali"), tr("Ocean"), tr("Rose"), tr("Amber"), NULL};
    state->theme_dropdown = gtk_drop_down_new_from_strings(theme_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->theme_dropdown), value_index(ui_config.theme, theme_codes, G_N_ELEMENTS(theme_codes), 0));
    gtk_box_append(GTK_BOX(page), setting_row("Theme", state->theme_dropdown));
    g_signal_connect(state->theme_dropdown, "notify::selected", G_CALLBACK(theme_changed), state);

    const char *shape_labels[] = {tr("Rounded"), tr("Soft"), tr("Square"), NULL};
    state->shape_dropdown = gtk_drop_down_new_from_strings(shape_labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->shape_dropdown), value_index(ui_config.shape, shape_codes, G_N_ELEMENTS(shape_codes), 1));
    gtk_box_append(GTK_BOX(page), setting_row("Shape", state->shape_dropdown));
    g_signal_connect(state->shape_dropdown, "notify::selected", G_CALLBACK(shape_changed), state);

    state->language_dropdown = gtk_drop_down_new_from_strings(language_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->language_dropdown), value_index(ui_config.language, language_codes, G_N_ELEMENTS(language_codes), 0));
    gtk_box_append(GTK_BOX(page), setting_row("Language", state->language_dropdown));
    g_signal_connect(state->language_dropdown, "notify::selected", G_CALLBACK(language_changed), state);

    state->settings_status = gtk_label_new(tr("Restart required to apply language."));
    gtk_widget_add_css_class(state->settings_status, "settings-status");
    gtk_label_set_wrap(GTK_LABEL(state->settings_status), TRUE);
    gtk_widget_set_halign(state->settings_status, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(page), state->settings_status);

    GtkWidget *about = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(about, "settings-row");
    GtkWidget *about_name = gtk_label_new(tr("Nebula Flasher"));
    gtk_widget_add_css_class(about_name, "settings-row-label");
    gtk_widget_set_hexpand(about_name, TRUE);
    gtk_widget_set_halign(about_name, GTK_ALIGN_START);
    char *about_text = g_strdup_printf("v%s | C + GTK4 | %s", APP_VERSION, tr("Native Linux application"));
    GtkWidget *about_meta = gtk_label_new(about_text);
    g_free(about_text);
    gtk_widget_add_css_class(about_meta, "muted-label");
    gtk_box_append(GTK_BOX(about), about_name);
    gtk_box_append(GTK_BOX(about), about_meta);
    gtk_box_append(GTK_BOX(page), about);
    return page;
}

static GtkWidget *build_ui(AppState *state) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "root-layout");

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(header, "hero");
    gtk_widget_set_margin_start(header, 28);
    gtk_widget_set_margin_end(header, 28);
    gtk_widget_set_margin_top(header, 24);
    gtk_widget_set_margin_bottom(header, 18);

    GtkWidget *brand = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(brand), "<span size='small' weight='bold' letter_spacing='18000'>NEBULA PROJECT</span>");
    gtk_widget_add_css_class(brand, "eyebrow");
    gtk_widget_set_halign(brand, GTK_ALIGN_START);

    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *title = gtk_label_new(tr("Nebula Flasher"));
    gtk_widget_add_css_class(title, "hero-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title, TRUE);
    state->settings_button = gtk_button_new_with_label(tr("Settings"));
    gtk_widget_add_css_class(state->settings_button, "secondary-button");
    gtk_widget_set_valign(state->settings_button, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(title_row), title);
    gtk_box_append(GTK_BOX(title_row), state->settings_button);

    GtkWidget *subtitle = gtk_label_new(tr("Create bootable USB drives from OS images."));
    gtk_widget_add_css_class(subtitle, "hero-subtitle");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header), brand);
    gtk_box_append(GTK_BOX(header), title_row);
    gtk_box_append(GTK_BOX(header), subtitle);
    gtk_box_append(GTK_BOX(root), header);

    state->content_stack = gtk_stack_new();
    gtk_widget_set_vexpand(state->content_stack, TRUE);
    gtk_stack_set_transition_type(GTK_STACK(state->content_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(state->content_stack), build_home_page(state), "home");
    gtk_stack_add_named(GTK_STACK(state->content_stack), build_settings_page(state), "settings");
    gtk_box_append(GTK_BOX(root), state->content_stack);
    g_signal_connect(state->settings_button, "clicked", G_CALLBACK(toggle_settings), state);
    return root;
}

static void apply_css(void) {
    const char *primary = theme_primary();
    const char *radius = shape_radius();
    char *css = g_strdup_printf(
        "window { background: #0d0a18; color: #f3efff; }"
        ".root-layout { background: #0d0a18; }"
        ".hero { background: linear-gradient(to right, #211342, #12162c); border-bottom: 1px solid #38265f; }"
        ".eyebrow { color: #9de7ff; letter-spacing: 3px; }"
        ".hero-title { color: #faf7ff; font-size: 30px; font-weight: 800; }"
        ".hero-subtitle { color: #c1b8dd; font-size: 14px; }"
        ".step-card { background: #171226; border: 1px solid #32264c; border-radius: %s; padding: 18px; }"
        ".step-number { background: %s; color: #ffffff; border-radius: %s; padding: 5px 8px; font-weight: 800; }"
        ".section-title { color: #f0eaff; font-size: 16px; font-weight: 700; }"
        ".settings-title { color: #f0eaff; font-size: 26px; font-weight: 800; }"
        ".settings-row { background: #111421; border: 1px solid #29334f; border-radius: %s; padding: 14px; }"
        ".settings-row-label { color: #e7e6f2; font-weight: 700; }"
        ".muted-label { color: #a9a0c3; }"
        ".status-label { color: #c8c0db; }"
        ".settings-status { color: #7f8fb8; font-size: 12px; }"
        ".footer-label { color: #8f86a9; font-size: 11px; }"
        ".secondary-button { background: #28203b; color: #f3edff; border: 1px solid #493666; border-radius: %s; padding: 8px 14px; }"
        ".secondary-button:hover { background: #382851; }"
        ".primary-button { background: %s; color: white; border-radius: %s; padding: 12px; font-weight: 800; }"
        ".primary-button:hover { background: %s; }"
        ".primary-button:disabled { background: #393149; color: #827b91; }"
        "progressbar trough { background: #2b223d; border-radius: %s; min-height: 12px; }"
        "progressbar progress { background: %s; border-radius: %s; min-height: 12px; }"
        "checkbutton { color: #d9d1ec; }"
        "dropdown, entry { background: #211a31; color: #f3edff; border-color: #483760; border-radius: %s; }",
        radius, primary, radius, radius, radius, primary, radius, primary, radius, primary, radius, radius);

    if (!global_css_provider) global_css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(global_css_provider, css, -1);
    GdkDisplay *display = gdk_display_get_default();
    if (display && !global_css_provider_installed) {
        gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(global_css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        global_css_provider_installed = TRUE;
    }
    g_free(css);
}

static gboolean on_window_close_request(GtkWindow *window, gpointer user_data) {
    (void)window;
    AppState *state = user_data;
    if (state->process) {
        set_status(state, tr("A flash is in progress. Wait for writing and verification to finish before closing the app."));
        return TRUE;
    }
    return FALSE;
}

static void on_activate(GtkApplication *application, gpointer user_data) {
    AppState *state = user_data;
    state->app = application;
    state->drives = g_ptr_array_new_with_free_func(drive_free);
    state->device_model = gtk_string_list_new(NULL);
    state->window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(state->window), "Nebula Flasher");
    gtk_window_set_default_size(GTK_WINDOW(state->window), 800, 720);
    gtk_window_set_resizable(GTK_WINDOW(state->window), TRUE);
    gtk_window_set_child(GTK_WINDOW(state->window), build_ui(state));
    g_signal_connect(state->window, "close-request", G_CALLBACK(on_window_close_request), state);
    apply_css();
    refresh_devices(state, FALSE);
    if (!state->drives->len) set_status(state, tr("Connect a USB drive to get started."));
    restart_refresh_timer(state);
    gtk_window_present(GTK_WINDOW(state->window));
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--version") == 0 || g_strcmp0(argv[i], "-V") == 0) {
            g_print("Nebula Flasher %s\n", APP_VERSION);
            return 0;
        }
        if (g_strcmp0(argv[i], "--help") == 0 || g_strcmp0(argv[i], "-h") == 0) {
            g_print("Nebula Flasher %s\n", APP_VERSION);
            g_print("Usage: nebula-flasher [--version|--help]\n");
            g_print("Write and verify an OS image on a removable USB drive.\n");
            return 0;
        }
    }
    setlocale(LC_ALL, "");
    load_ui_config();
    AppState state = {0};
    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &state);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    if (state.process) {
        g_subprocess_force_exit(state.process);
        g_clear_object(&state.process);
    }
    g_clear_object(&state.output);
    if (state.drives) g_ptr_array_unref(state.drives);
    if (state.device_model) g_object_unref(state.device_model);
    if (state.refresh_source) g_source_remove(state.refresh_source);
    if (global_css_provider) g_object_unref(global_css_provider);
    g_free(state.image_path);
    g_free(state.last_error);
    g_object_unref(app);
    return status;
}
