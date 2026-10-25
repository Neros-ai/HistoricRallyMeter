#include "nero_frame.h"
#include "callbacks.h"

// Style class shared with the rest of the co-pilot's keys (20 px bold).
static void addClass(GtkWidget* w, const char* cls) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

GtkWidget* createNeroKeypad(AppData* data) {
    GtkWidget* keypad = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(keypad), nero::KEYPAD_GAP);
    gtk_grid_set_column_spacing(GTK_GRID(keypad), nero::KEYPAD_GAP);

    const int n = nero::KEYPAD_COLS * nero::KEYPAD_ROWS;
    for (int i = 0; i < n; i++) {
        const char* label = nero::KEYPAD_LABELS[i];
        GtkWidget* btn = gtk_button_new_with_label(label);
        gtk_widget_set_size_request(btn, nero::KEYPAD_KEY_W, nero::KEYPAD_KEY_H);
        addClass(btn, "dist-heading");   // 28 px bold monospace, as the menu key (nav-button was 20 px)
        GCallback cb = G_CALLBACK(on_keypad_digit);
        if (nero::isBackspaceLabel(label))
            cb = G_CALLBACK(on_keypad_backspace);
        else if (std::strcmp(label, "C") == 0)
            cb = G_CALLBACK(on_keypad_clear);
        g_signal_connect(btn, "clicked", cb, data);
        gtk_grid_attach(GTK_GRID(keypad), btn, i % nero::KEYPAD_COLS, i / nero::KEYPAD_COLS, 1, 1);
    }
    return keypad;
}

GtkWidget* createMenuKey(AppData* data) {
    GtkWidget* btn = gtk_button_new_with_label("menu");
    gtk_widget_set_size_request(btn, nero::MENU_KEY_W, nero::MENU_KEY_H);
    addClass(btn, "dist-heading");   // 28 px bold, like total / trip (nav-button was 20 px)
    g_signal_connect(btn, "clicked", G_CALLBACK(on_show_menu), data);
    return btn;
}

// Build after every page the menu can open has been added to the stack: a
// key whose page does not exist yet (Settings, until it is built) is greyed
// rather than left to point at nothing.
GtkWidget* createMenuScreen(AppData* data) {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), nero::MENU_GAP);
    gtk_grid_set_column_spacing(GTK_GRID(grid), nero::MENU_GAP);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 5);

    for (int i = 0; i < 6; i++) {
        const nero::MenuItem* item = &nero::MENU_ITEMS[i];
        GtkWidget* btn = gtk_button_new();
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* title = gtk_label_new(item->label);
        GtkWidget* caption = gtk_label_new(item->caption);
        addClass(title, "menu-item");
        addClass(caption, "menu-caption");
        gtk_box_pack_start(GTK_BOX(box), title, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), caption, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(btn), box);
        gtk_widget_set_hexpand(btn, TRUE);
        gtk_widget_set_vexpand(btn, TRUE);

        g_object_set_data(G_OBJECT(btn), "menu-item", const_cast<nero::MenuItem*>(item));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_menu_choice), data);
        if (item->page && !gtk_stack_get_child_by_name(data->copilotStack, item->page))
            gtk_widget_set_sensitive(btn, FALSE);

        gtk_grid_attach(GTK_GRID(grid), btn, i % nero::MENU_COLS, i / nero::MENU_COLS, 1, 1);
    }
    return grid;
}
