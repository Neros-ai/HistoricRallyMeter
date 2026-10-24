#include "ui_datetime.h"
#include "callbacks.h"
#include "calculations.h"
#include "config_file.h"
#include "nero_frame.h"
#include "rally_state.h"
#include "stage_rows.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace {

constexpr int INFO_W = 1012, KP_W = 240;
constexpr int ROW_H = 66, ROW_GAP = 12;
constexpr int LABEL_W = 250, VALUE_W = 360, KEY_W = 260;

enum { RALLY, COMPUTER, PHONE, OFFSET };

int64_t systemNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// "hh:mm:ss" and the tenths.
void splitTime(int64_t ms, std::string& hms, std::string& tenth) {
    time_t s = ms / 1000;
    struct tm* t = localtime(&s);
    char b[16];
    if (t) snprintf(b, sizeof(b), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    else snprintf(b, sizeof(b), "--:--:--");
    hms = b;
    long frac = ((ms % 1000) + 1000) % 1000;
    snprintf(b, sizeof(b), ".%ld", frac / 100);
    tenth = b;
}

bool phoneKnown(AppData* d) {
    return d->phoneClockMs != 0 && systemNowMs() - d->phoneClockSeenMs < 5000;
}

gboolean onDraw(GtkWidget* w, cairo_t* cr, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    const int kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "kind"));
    const int width = gtk_widget_get_allocated_width(w);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    auto big = [&](const std::string& hms, const std::string& tenth, bool cyan) {
        if (cyan) cairo_set_source_rgb(cr, 0, 1, 1); else cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_font_size(cr, 56);
        cairo_move_to(cr, 0, 52);
        cairo_show_text(cr, hms.c_str());
        cairo_text_extents_t e;
        cairo_text_extents(cr, hms.c_str(), &e);
        cairo_set_font_size(cr, 42);   // tenths: three quarters of the seconds
        cairo_move_to(cr, e.x_advance, 52);
        cairo_show_text(cr, tenth.c_str());
    };

    std::string hms, tenth;
    if (kind == RALLY) {
        splitTime(getRallyTime_ms(*d->state), hms, tenth);
        big(hms, tenth, true);
    } else if (kind == COMPUTER) {
        splitTime(systemNowMs(), hms, tenth);
        big(hms, tenth, false);
    } else if (kind == PHONE) {
        if (phoneKnown(d)) {
            splitTime(d->phoneClockMs + (systemNowMs() - d->phoneClockSeenMs), hms, tenth);
            big(hms, tenth, false);
        } else {
            cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
            cairo_set_font_size(cr, 40);
            cairo_move_to(cr, 0, 48);
            cairo_show_text(cr, "no phone");
        }
    } else {  // OFFSET: "Computer" at the left, the signed offset right-aligned over the tenths
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_font_size(cr, 26);
        cairo_move_to(cr, 0, 46);
        cairo_show_text(cr, "Computer");
        std::string off = formatOffsetSeconds(d->state->rallyTimeOffset_ms);
        cairo_set_source_rgb(cr, 0, 1, 1);
        cairo_set_font_size(cr, 56);
        cairo_text_extents_t e;
        cairo_text_extents(cr, off.c_str(), &e);
        cairo_move_to(cr, VALUE_W - e.x_advance, 52);
        cairo_show_text(cr, off.c_str());
        // date, right of the offset
        time_t s = getRallyTime_ms(*d->state) / 1000;
        char b[40] = "";
        if (struct tm* t = localtime(&s)) strftime(b, sizeof(b), "%a %d %b %Y", t);
        cairo_set_source_rgb(cr, 0.67, 0.67, 0.67);
        cairo_set_font_size(cr, 26);
        cairo_move_to(cr, VALUE_W + 24, 46);
        cairo_show_text(cr, b);
    }
    (void)width;
    return FALSE;
}

GtkWidget* makeArea(AppData* d, int kind, int w, int h) {
    GtkWidget* a = gtk_drawing_area_new();
    gtk_widget_set_size_request(a, w, h);
    g_object_set_data(G_OBJECT(a), "kind", GINT_TO_POINTER(kind));
    g_signal_connect(a, "draw", G_CALLBACK(onDraw), d);
    d->dtAreas[kind] = a;
    return a;
}

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* makeKey(const char* label, int w, int h, GCallback cb, AppData* d) {
    GtkWidget* b = gtk_button_new_with_label(label);
    gtk_widget_set_size_request(b, w, h);
    addClass(b, "dist-heading");
    addClass(b, "key-small");
    gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(b))), GTK_JUSTIFY_CENTER);
    if (cb) g_signal_connect(b, "clicked", cb, d);
    return b;
}

GtkWidget* labelCell(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    addClass(l, "dt-label");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_widget_set_size_request(l, LABEL_W, ROW_H);
    return l;
}

// Zero the offset: the rally clock follows the computer clock exactly.
void onRallyToComputer(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    d->state->rallyTimeOffset_ms = 0;
    ConfigFile::save(*d->state);
    notifyWebStateNow(d);
}

// Today's date (rally clock) at the entered time of day: the rally clock then
// reads that time now, and the computer clock is left alone.
void onRallyToEntered(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    const long tod = parseTimeOfDay(gtk_entry_get_text(d->timeEntry));
    if (tod < 0) {
        flashNotice(w, "Enter the rally time as hh:mm or hh:mm:ss.");
        return;
    }
    // autoStartTargetForTimeOfDay gives that time of day in the rally clock's
    // own frame, on its current date; the offset is that less the computer's now.
    d->state->rallyTimeOffset_ms = autoStartTargetForTimeOfDay(d, tod) - systemNowMs();
    ConfigFile::save(*d->state);
    notifyWebStateNow(d);
    gtk_entry_set_text(d->timeEntry, "");
}

// Sets the computer clock to the phone's. The rally clock is the computer
// clock plus an offset, so the offset is moved by the same step and the rally
// clock itself does not jump.
void onComputerToPhone(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (!phoneKnown(d)) return;
    const int64_t phone_now = d->phoneClockMs + (systemNowMs() - d->phoneClockSeenMs);
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "sudo -n date -s @%lld >/dev/null 2>&1",
             static_cast<long long>(phone_now / 1000));
    const int64_t before = systemNowMs();
    if (system(cmd) != 0) {
        flashNotice(w, "The computer clock could not be set.");
        return;
    }
    d->state->rallyTimeOffset_ms += before - systemNowMs();
    ConfigFile::save(*d->state);
    notifyWebStateNow(d);
}

}  // namespace

GtkWidget* createDateTimeScreen(AppData* d) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);

    // The old labels some update code still writes; never shown.
    d->systemClockLabel = GTK_LABEL(gtk_label_new(""));
    d->systemTimeLabel = GTK_LABEL(gtk_label_new(""));
    d->rallyClockLabel = GTK_LABEL(gtk_label_new(""));
    d->rallyTimeLabel = GTK_LABEL(gtk_label_new(""));
    d->dateEntry = GTK_ENTRY(gtk_entry_new());

    GtkWidget* info = gtk_box_new(GTK_ORIENTATION_VERTICAL, ROW_GAP);
    gtk_widget_set_size_request(info, INFO_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), info, FALSE, FALSE, 0);

    auto row = [&]() {
        GtkWidget* r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ROW_GAP);
        gtk_widget_set_size_request(r, INFO_W, ROW_H);
        gtk_box_pack_start(GTK_BOX(info), r, FALSE, FALSE, 0);
        return r;
    };

    // Rally time, with its trim
    GtkWidget* r1 = row();
    gtk_box_pack_start(GTK_BOX(r1), labelCell("Rally time"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r1), makeArea(d, RALLY, VALUE_W, ROW_H), FALSE, FALSE, 0);
    for (int steps : {-1, 1}) {
        GtkWidget* b = makeKey(steps < 0 ? "-0.1" : "+0.1", 100, 60, nullptr, d);
        g_object_set_data(G_OBJECT(b), "trim-steps", GINT_TO_POINTER(steps));
        // "pressed"/"released" so holding the key repeats.
        g_signal_connect(b, "pressed", G_CALLBACK(on_trim_rally_pressed), d);
        g_signal_connect(b, "released", G_CALLBACK(on_trim_rally_released), d);
        gtk_box_pack_start(GTK_BOX(r1), b, FALSE, FALSE, 0);
    }

    GtkWidget* r2 = row();
    gtk_box_pack_start(GTK_BOX(r2), labelCell("Computer time"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r2), makeArea(d, COMPUTER, VALUE_W, ROW_H), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r2), makeKey("set rally to\ncomputer time", KEY_W, 60, G_CALLBACK(onRallyToComputer), d), FALSE, FALSE, 0);

    GtkWidget* r3 = row();
    gtk_box_pack_start(GTK_BOX(r3), labelCell("Phone time"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r3), makeArea(d, PHONE, VALUE_W, ROW_H), FALSE, FALSE, 0);
    d->dtSetFromPhoneBtn = makeKey("set computer to\nphone time", KEY_W, 60, G_CALLBACK(onComputerToPhone), d);
    gtk_widget_set_sensitive(d->dtSetFromPhoneBtn, FALSE);
    gtk_box_pack_start(GTK_BOX(r3), d->dtSetFromPhoneBtn, FALSE, FALSE, 0);

    GtkWidget* r4 = row();
    gtk_box_pack_start(GTK_BOX(r4), labelCell("Enter rally time"), FALSE, FALSE, 0);
    d->timeEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_width_chars(d->timeEntry, 1);
    gtk_widget_set_size_request(GTK_WIDGET(d->timeEntry), VALUE_W, ROW_H);
    addClass(GTK_WIDGET(d->timeEntry), "dt-entry");
    g_signal_connect(d->timeEntry, "focus-in-event", G_CALLBACK(on_entry_focus), d);
    gtk_box_pack_start(GTK_BOX(r4), GTK_WIDGET(d->timeEntry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r4), makeKey("set rally to\nentered time", KEY_W, 60, G_CALLBACK(onRallyToEntered), d), FALSE, FALSE, 0);

    GtkWidget* r5 = row();
    gtk_box_pack_start(GTK_BOX(r5), labelCell("Offset"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(r5), makeArea(d, OFFSET, 700, ROW_H), FALSE, FALSE, 0);

    // keypad column
    GtkWidget* kp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(kp, KP_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), kp, FALSE, FALSE, 0);
    d->datetimeKeypad = createNeroKeypad(d);
    gtk_box_pack_start(GTK_BOX(kp), d->datetimeKeypad, FALSE, FALSE, 0);
    GtkWidget* menu = createMenuKey(d);
    gtk_widget_set_halign(menu, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(kp), menu, FALSE, FALSE, 0);

    // Speed units live on Settings; main.cpp wires this button's handler at
    // startup, so it has to exist until that page builds its own.
    d->unitToggleBtn = GTK_BUTTON(gtk_button_new_with_label(d->state->units ? "MPH" : "KPH"));
    g_object_ref_sink(d->unitToggleBtn);
    return screen;
}

void updateDateTimeDisplay(AppData* d) {
    for (GtkWidget* a : d->dtAreas) if (a) gtk_widget_queue_draw(a);
    if (d->dtSetFromPhoneBtn)
        gtk_widget_set_sensitive(d->dtSetFromPhoneBtn, phoneKnown(d));
}
