#include "settings_dialog.h"
#include "glib.h"
#include "src/gui/app_window.h"
#include "src/gui/theming.h"
#include "src/settings/definitions.h"
#include "src/settings/settings.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

static LSGuiSetting* gui_settings;
static LSAppWindow* g_main_window = NULL; // Reference to main window

static size_t enumerate_settings(AppConfig cfg)
{
    int settings_number = 0;
    for (size_t s = 0; s < sections_count; ++s) {
        SectionInfo section_info = sections[s];
        if (!section_info.in_gui) {
            continue;
        }
        settings_number += section_info.count;
    }
    return settings_number;
}

static gboolean on_help_window_delete(GtkWidget* widget, GdkEvent* event, gpointer user_data)
{
    gtk_widget_destroy(widget);
    free(gui_settings);
    return TRUE;
}

/**
 * Shows an error dialog for invalid theme names.
 *
 * @param parent_window The parent window for the dialog.
 * @param theme_name The invalid theme name.
 * @param theme_variant The invalid theme variant (can be NULL).
 */
static void show_theme_error_dialog(GtkWindow* parent_window, const char* theme_name, const char* theme_variant)
{
    char message[512];
    if (theme_variant && strlen(theme_variant) > 0) {
        snprintf(message, sizeof(message),
            "Theme not found: \"%s\" (variant: \"%s\")\n\n"
            "Please check the theme name and variant are correct.\n"
            "The theme files should be located in:\n"
            "$XDG_CONFIG_HOME/libresplit/themes/%s/%s-%s.css",
            theme_name, theme_variant, theme_name, theme_name, theme_variant);
    } else {
        snprintf(message, sizeof(message),
            "Theme not found: \"%s\"\n\n"
            "Please check the theme name is correct.\n"
            "The theme file should be located in:\n"
            "$XDG_CONFIG_HOME/libresplit/themes/%s/%s.css",
            theme_name, theme_name, theme_name);
    }

    GtkWidget* dialog = gtk_message_dialog_new(
        parent_window,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s", message);

    gtk_window_set_title(GTK_WINDOW(dialog), "Theme Not Found");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/**
 * Saves the GUI settings and validates theme changes.
 * If theme settings have changed, validates the new theme exists and
 * automatically refreshes the main window if valid.
 *
 * @param action The action that triggered this callback
 * @param parameter Additional parameters (unused)
 * @param app Pointer to the GTK application
 */
static void save_gui_settings(GSimpleAction* action, GVariant* parameter, gpointer app)
{
    // get the current theme settings to compare later
    char old_theme_name[4096];
    char old_theme_variant[4096];
    strcpy(old_theme_name, cfg.theme.name.value.s);
    strcpy(old_theme_variant, cfg.theme.variant.value.s);

    size_t settings_number = enumerate_settings(cfg);

    // Parse all values in gui_settings, assign them to the respective cfg settings
    for (size_t i = 0; i < settings_number; i++) {
        LSGuiSetting setting_to_save = gui_settings[i];
        switch (setting_to_save.settings_entry->type) {
            case CFG_STRING:
            case CFG_KEYBIND: // TODO: Keygrab logic for keybinds
                const char* str_value = gtk_entry_buffer_get_text(setting_to_save.entry_buffer);
                strcpy(setting_to_save.settings_entry->value.s, str_value);
                break;
            case CFG_BOOL:
                bool bool_value = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(setting_to_save.widget));
                setting_to_save.settings_entry->value.b = bool_value;
                break;
            case CFG_INT:
                const char* int_str_value = gtk_entry_buffer_get_text(setting_to_save.entry_buffer);
                int int_value = atoi(int_str_value);
                setting_to_save.settings_entry->value.i = int_value;
                break;
        }
    }

    // check if theme settings have changed and validate them
    bool theme_changed = (strcmp(old_theme_name, cfg.theme.name.value.s) != 0) || (strcmp(old_theme_variant, cfg.theme.variant.value.s) != 0);

    if (theme_changed && g_main_window) {
        // validate the new theme (only if theme name is not empty)
        if (strlen(cfg.theme.name.value.s) > 0) {
            char theme_path[PATH_MAX];
            int theme_found = ls_app_window_find_theme(g_main_window,
                cfg.theme.name.value.s,
                cfg.theme.variant.value.s,
                theme_path);

            if (!theme_found) {
                // Theme doesn't exist, show error and restore old values
                show_theme_error_dialog(NULL, cfg.theme.name.value.s, cfg.theme.variant.value.s);

                // Restore the old theme settings
                strcpy(cfg.theme.name.value.s, old_theme_name);
                strcpy(cfg.theme.variant.value.s, old_theme_variant);

                return; // Don't save settings if theme is invalid
            }
        }

        // theme is valid (or empty for fallback), refresh the main window
        ls_app_refresh_theme(g_main_window);
    }

    // Save all the settings
    config_save();
}

static void set_widget_defaults(GtkWidget* obj)
{
    gtk_widget_set_margin_top(obj, 8);
    gtk_widget_set_margin_bottom(obj, 8);
    gtk_widget_set_margin_start(obj, 8);
    gtk_widget_set_margin_end(obj, 8);
    gtk_widget_set_vexpand(obj, TRUE);
    gtk_widget_set_hexpand(obj, TRUE);
}

static void build_settings_dialog(GtkApplication* app, gpointer data)
{
    int settings_number = enumerate_settings(cfg);
    gui_settings = malloc(settings_number * sizeof(LSGuiSetting));

    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "LibreSplit Settings");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 500);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "delete-event", G_CALLBACK(on_help_window_delete), NULL);
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    set_widget_defaults(main_box);
    GtkWidget* tabs = gtk_notebook_new();
    set_widget_defaults(tabs);
    int settings_idx = 0;
    for (size_t s = 0; s < sections_count; ++s) {
        SectionInfo section_info = sections[s];
        if (!section_info.in_gui) {
            continue;
        }
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        set_widget_defaults(box);
        GtkWidget* title = gtk_accel_label_new(section_info.name);
        for (size_t i = 0; i < section_info.count; ++i) {
            ConfigEntry entry = ((ConfigEntry*)section_info.entries)[i];
            gui_settings[settings_idx].settings_entry = &((ConfigEntry*)section_info.entries)[i];
            switch (entry.type) {
                case CFG_STRING:
                    GtkWidget* lbl_str = gtk_label_new(entry.desc);
                    gtk_container_add(GTK_CONTAINER(box), lbl_str);

                    gui_settings[settings_idx].entry_buffer = gtk_entry_buffer_new(entry.value.s, sizeof(entry.value.s));
                    gui_settings[settings_idx].widget = gtk_entry_new_with_buffer(gui_settings[settings_idx].entry_buffer);
                    gtk_container_add(GTK_CONTAINER(box), gui_settings[settings_idx].widget);
                    break;
                case CFG_KEYBIND:
                    GtkWidget* lbl_kb = gtk_label_new(entry.desc);
                    gtk_container_add(GTK_CONTAINER(box), lbl_kb);

                    gui_settings[settings_idx].entry_buffer = gtk_entry_buffer_new(entry.value.s, sizeof(entry.value.s));
                    gui_settings[settings_idx].widget = gtk_entry_new_with_buffer(gui_settings[settings_idx].entry_buffer);
                    gtk_container_add(GTK_CONTAINER(box), gui_settings[settings_idx].widget);
                    break;
                case CFG_BOOL:
                    gui_settings[settings_idx].widget = gtk_check_button_new_with_label(entry.desc);
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui_settings[settings_idx].widget), entry.value.b);
                    gtk_container_add(GTK_CONTAINER(box), gui_settings[settings_idx].widget);
                    break;
                case CFG_INT:
                    GtkWidget* lbl_int = gtk_label_new(entry.desc);
                    gtk_container_add(GTK_CONTAINER(box), lbl_int);
                    char setting_as_str[64];
                    sprintf(setting_as_str, "%d", entry.value.i);

                    gui_settings[settings_idx].entry_buffer = gtk_entry_buffer_new(setting_as_str, sizeof(setting_as_str));
                    gui_settings[settings_idx].widget = gtk_entry_new_with_buffer(gui_settings[settings_idx].entry_buffer);
                    gtk_container_add(GTK_CONTAINER(box), gui_settings[settings_idx].widget);
                    break;
            }
            settings_idx++;
        }
        gtk_notebook_append_page(GTK_NOTEBOOK(tabs), box, title);
    }
    gtk_container_add(GTK_CONTAINER(main_box), tabs);
    GtkWidget* save_btn = gtk_button_new_with_label("Save");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(save_gui_settings), NULL);
    gtk_container_add(GTK_CONTAINER(main_box), save_btn);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    gtk_widget_show_all(main_box);
    gtk_window_present(GTK_WINDOW(window));
}

void show_settings_dialog(GSimpleAction* action, GVariant* parameter, gpointer app)
{
    if (parameter != NULL) {
        app = parameter;
    }

    // Set the global main window reference
    GList* windows = gtk_application_get_windows(GTK_APPLICATION(app));
    g_main_window = windows ? (LSAppWindow*)(windows->data) : NULL;

    build_settings_dialog(GTK_APPLICATION(app), NULL);
}
