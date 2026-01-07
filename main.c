#include "orangehrm_client.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

#define APP_ICON_PATH "assets/icon.png"
#define TIMER_INTERVAL_MS 1000

/* Global state */
static time_t g_start_time = 0;
static time_t g_stop_time = 0;
static guint g_timer_id = 0;
static GtkWidget *g_time_label = NULL;
static GtkWidget *g_main_window = NULL;
static Config g_config;
static pthread_mutex_t g_config_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Thread data structure for passing punch data safely
 */
typedef struct {
    time_t start_time;
    time_t stop_time;
    char username[256];
    char base_url[512];
    char client_id[256];
    char client_secret[256];
    char password[256];
    char type[64];
} PunchThreadData;

/**
 * Set window icon from file
 */
static void set_window_icon(GtkWindow *win, const char *icon_path) {
    GError *err = NULL;
    gtk_window_set_icon_from_file(win, icon_path, &err);
    if (err) {
        g_printerr("Failed to set icon (%s): %s\n", icon_path, err->message);
        g_error_free(err);
    }
}

/**
 * Write a message to the application log file
 */
static void write_log(const char *message) {
    FILE *log_file = fopen("application.log", "a");
    if (log_file == NULL) {
        fprintf(stderr, "Error opening log file\n");
        return;
    }
    
    time_t current_time;
    struct tm *tm_info;
    char time_str[20];
    
    time(&current_time);
    tm_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log_file, "[%s] %s\n", time_str, message);
    fclose(log_file);
}

/**
 * Format a time_t into day, time, and timezone strings
 */
static void format_time(time_t raw_time, char *formatted_day, size_t day_size,
                        char *formatted_time, size_t time_size,
                        char *time_zone, size_t tz_size) {
    struct tm *tm_info = localtime(&raw_time);
    if (tm_info == NULL) {
        snprintf(formatted_day, day_size, "1970-01-01");
        snprintf(formatted_time, time_size, "00:00");
        snprintf(time_zone, tz_size, "+0.0");
        return;
    }
    
    int timezone_offset_in_minutes = (int)(tm_info->tm_gmtoff / 60);
    strftime(formatted_day, day_size, "%Y-%m-%d", tm_info);
    strftime(formatted_time, time_size, "%H:%M", tm_info);
    float hours_offset = timezone_offset_in_minutes / 60.0f;
    snprintf(time_zone, tz_size, "%+05.1f", hours_offset);
}

/**
 * Format elapsed time in seconds to HH:MM:SS string
 */
static void format_elapsed_time(double elapsed_time, char *formatted_time, size_t size) {
    int hours = (int)(elapsed_time / 3600);
    int minutes = (int)((elapsed_time - (hours * 3600)) / 60);
    int seconds = (int)(elapsed_time - (hours * 3600) - (minutes * 60));
    snprintf(formatted_time, size, "%02d:%02d:%02d", hours, minutes, seconds);
}

/* Toast notification constants */
#define TOAST_DISPLAY_MS 3000      /* How long to show toast */
#define TOAST_FADE_INTERVAL_MS 50  /* Fade animation interval */
#define TOAST_FADE_STEP 0.05       /* Opacity decrease per step */

/**
 * Toast notification data structure
 */
typedef struct {
    GtkWidget *window;
    gdouble opacity;
    guint fade_timer_id;
} ToastData;

/**
 * Fade out timer callback
 */
static gboolean toast_fade_out(gpointer data) {
    ToastData *toast = (ToastData *)data;
    
    toast->opacity -= TOAST_FADE_STEP;
    
    if (toast->opacity <= 0.0) {
        gtk_widget_destroy(toast->window);
        free(toast);
        return FALSE;  /* Stop timer */
    }
    
    gtk_widget_set_opacity(toast->window, toast->opacity);
    return TRUE;  /* Continue fading */
}

/**
 * Start fade out after display time
 */
static gboolean toast_start_fade(gpointer data) {
    ToastData *toast = (ToastData *)data;
    toast->fade_timer_id = g_timeout_add(TOAST_FADE_INTERVAL_MS, toast_fade_out, toast);
    return FALSE;  /* Don't repeat */
}

/**
 * Create and show a toast notification
 */
static void show_toast(const char *message, gboolean is_error) {
    ToastData *toast = (ToastData *)malloc(sizeof(ToastData));
    if (toast == NULL) return;
    
    toast->opacity = 1.0;
    toast->fade_timer_id = 0;
    
    /* Create popup window */
    toast->window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_decorated(GTK_WINDOW(toast->window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(toast->window), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(toast->window), TRUE);
    gtk_widget_set_size_request(toast->window, 280, 50);
    
    /* Create label with message */
    GtkWidget *label = gtk_label_new(message);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 35);
    gtk_container_add(GTK_CONTAINER(toast->window), label);
    
    /* Style the toast */
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css = is_error 
        ? "window { background-color: #c62828; border-radius: 8px; padding: 12px; }"
          "label { color: white; font-size: 12px; font-weight: bold; }"
        : "window { background-color: #2e7d32; border-radius: 8px; padding: 12px; }"
          "label { color: white; font-size: 12px; font-weight: bold; }";
    
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(toast->window),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    gtk_style_context_add_provider(gtk_widget_get_style_context(label),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
    
    /* Position near main window (bottom right) */
    if (g_main_window != NULL) {
        gint win_x, win_y, win_width, win_height;
        gtk_window_get_position(GTK_WINDOW(g_main_window), &win_x, &win_y);
        gtk_window_get_size(GTK_WINDOW(g_main_window), &win_width, &win_height);
        gtk_window_move(GTK_WINDOW(toast->window), win_x, win_y + win_height + 10);
    }
    
    gtk_widget_show_all(toast->window);
    
    /* Start fade timer after display time */
    g_timeout_add(TOAST_DISPLAY_MS, toast_start_fade, toast);
}

/**
 * Show toast from main thread (idle callback)
 */
typedef struct {
    char *message;
    gboolean is_error;
} ToastMessage;

static gboolean show_toast_idle(gpointer data) {
    ToastMessage *msg = (ToastMessage *)data;
    show_toast(msg->message, msg->is_error);
    free(msg->message);
    free(msg);
    return FALSE;
}

/**
 * Show error toast from any thread
 */
static void show_error_async(const char *message) {
    ToastMessage *msg = (ToastMessage *)malloc(sizeof(ToastMessage));
    if (msg != NULL) {
        msg->message = strdup(message);
        msg->is_error = TRUE;
        if (msg->message != NULL) {
            g_idle_add(show_toast_idle, msg);
        } else {
            free(msg);
        }
    }
}

/**
 * Show success toast from any thread
 */
static void show_success_async(const char *message) {
    ToastMessage *msg = (ToastMessage *)malloc(sizeof(ToastMessage));
    if (msg != NULL) {
        msg->message = strdup(message);
        msg->is_error = FALSE;
        if (msg->message != NULL) {
            g_idle_add(show_toast_idle, msg);
        } else {
            free(msg);
        }
    }
}

/**
 * Thread function to submit attendance record
 */
static void* submit_attendance_thread(void *data) {
    PunchThreadData *punch_data = (PunchThreadData *)data;
    int success = 1;
    Config thread_config;
    ResponseBuffer resp;
    struct json_object *json_obj = NULL;
    struct json_object *parsed_response = NULL;
    
    /* Initialize response buffer */
    if (response_buffer_init(&resp, MAX_RESPONSE_SIZE) != 0) {
        show_error_async("Failed to allocate memory for response");
        free(punch_data);
        return NULL;
    }
    
    /* Format times */
    char formatted_start_day[32], formatted_start_time[32], start_time_zone[16];
    char formatted_end_day[32], formatted_end_time[32], end_time_zone[16];

    format_time(punch_data->start_time, 
                formatted_start_day, sizeof(formatted_start_day),
                formatted_start_time, sizeof(formatted_start_time),
                start_time_zone, sizeof(start_time_zone));
    
    format_time(punch_data->stop_time,
                formatted_end_day, sizeof(formatted_end_day),
                formatted_end_time, sizeof(formatted_end_time),
                end_time_zone, sizeof(end_time_zone));

    /* Build JSON request */
    json_obj = json_object_new_object();
    if (json_obj == NULL) {
        show_error_async("Failed to create JSON object");
        success = 0;
        goto cleanup;
    }
    
    json_object_object_add(json_obj, "empNumber", json_object_new_string(punch_data->username));
    json_object_object_add(json_obj, "punchInDate", json_object_new_string(formatted_start_day));
    json_object_object_add(json_obj, "punchInTime", json_object_new_string(formatted_start_time));
    json_object_object_add(json_obj, "punchInTimezoneOffset", json_object_new_string(start_time_zone));
    json_object_object_add(json_obj, "punchInNote", json_object_new_string("App In"));
    json_object_object_add(json_obj, "punchOutDate", json_object_new_string(formatted_end_day));
    json_object_object_add(json_obj, "punchOutTime", json_object_new_string(formatted_end_time));
    json_object_object_add(json_obj, "punchOutTimezoneOffset", json_object_new_string(end_time_zone));
    json_object_object_add(json_obj, "punchOutNote", json_object_new_string("App out"));

    const char *json_string = json_object_to_json_string_ext(json_obj, JSON_C_TO_STRING_PRETTY);
    printf("Sending attendance record:\n%s\n", json_string);
    
    /* Set up thread-local config */
    memset(&thread_config, 0, sizeof(Config));
    thread_config.base_url = punch_data->base_url;
    thread_config.username = punch_data->username;
    thread_config.password = punch_data->password;
    thread_config.client_id = punch_data->client_id;
    thread_config.client_secret = punch_data->client_secret;
    thread_config.type = punch_data->type;
    thread_config.access_token = NULL;
    
    /* Get access token */
    if (get_token(&thread_config) != 0) {
        write_log("Failed to obtain access token");
        write_log(json_string);
        show_error_async("Failed to obtain access token. Check your credentials.");
        success = 0;
        goto cleanup;
    }

    /* Submit attendance record */
    if (post_request("/api/attendanceRecords", json_string, &thread_config, &resp) != 0) {
        write_log("POST request failed");
        write_log(json_string);
        show_error_async("Failed to submit attendance record. Network error.");
        success = 0;
        goto cleanup;
    }

    /* Parse response */
    parsed_response = json_tokener_parse(resp.buffer);
    if (parsed_response == NULL) {
        write_log("Error parsing JSON response");
        write_log(resp.buffer);
        show_error_async("Invalid response from server");
        success = 0;
        goto cleanup;
    }

    /* Check success field in response */
    struct json_object *success_obj;
    if (json_object_object_get_ex(parsed_response, "success", &success_obj)) {
        const char *success_str = json_object_get_string(success_obj);
        if (success_str != NULL && strcmp(success_str, "false") == 0) {
            write_log("Server returned success=false");
            write_log(resp.buffer);
            show_error_async("Server rejected the attendance record");
            success = 0;
        }
    }
    
    if (success) {
        show_success_async("Attendance record submitted successfully!");
    }

cleanup:
    if (json_obj != NULL) {
        json_object_put(json_obj);
    }
    if (parsed_response != NULL) {
        json_object_put(parsed_response);
    }
    
    /* Free token if allocated */
    free(thread_config.access_token);
    
    response_buffer_free(&resp);
    free(punch_data);
    
    return NULL;
}

/**
 * Timer callback to update the elapsed time display
 */
static gboolean update_time_label(gpointer data) {
    (void)data;  /* Unused */
    
    time_t current_time;
    time(&current_time);
    
    double elapsed_time = (g_start_time != 0) ? difftime(current_time, g_start_time) : 0;
    
    char formatted_time[32];
    format_elapsed_time(elapsed_time, formatted_time, sizeof(formatted_time));
    gtk_label_set_text(GTK_LABEL(g_time_label), formatted_time);
    
    return TRUE;  /* Continue timer */
}

/**
 * Handler for Start button click
 */
static void on_start_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *stop_button = GTK_WIDGET(data);
    
    gtk_widget_set_sensitive(widget, FALSE);
    gtk_widget_set_sensitive(stop_button, TRUE);
    
    time(&g_start_time);
    g_timer_id = g_timeout_add(TIMER_INTERVAL_MS, update_time_label, NULL);
}

/**
 * Handler for Stop button click
 */
static void on_stop_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *start_button = GTK_WIDGET(data);
    
    /* Stop the timer */
    if (g_timer_id != 0) {
        g_source_remove(g_timer_id);
        g_timer_id = 0;
    }

    time(&g_stop_time);
    
    /* Update display with final time */
    double elapsed_time = difftime(g_stop_time, g_start_time);
    char formatted_time[32];
    format_elapsed_time(elapsed_time, formatted_time, sizeof(formatted_time));
    gtk_label_set_text(GTK_LABEL(g_time_label), formatted_time);

    /* Re-enable buttons */
    gtk_widget_set_sensitive(start_button, TRUE);
    gtk_widget_set_sensitive(widget, FALSE);
    
    /* Prepare thread data (copy config values for thread safety) */
    PunchThreadData *punch_data = (PunchThreadData *)calloc(1, sizeof(PunchThreadData));
    if (punch_data == NULL) {
        show_error_async("Memory allocation failed");
        return;
    }
    
    punch_data->start_time = g_start_time;
    punch_data->stop_time = g_stop_time;
    
    /* Copy config values under mutex protection */
    pthread_mutex_lock(&g_config_mutex);
    if (g_config.username != NULL) {
        strncpy(punch_data->username, g_config.username, sizeof(punch_data->username) - 1);
    }
    if (g_config.base_url != NULL) {
        strncpy(punch_data->base_url, g_config.base_url, sizeof(punch_data->base_url) - 1);
    }
    if (g_config.client_id != NULL) {
        strncpy(punch_data->client_id, g_config.client_id, sizeof(punch_data->client_id) - 1);
    }
    if (g_config.client_secret != NULL) {
        strncpy(punch_data->client_secret, g_config.client_secret, sizeof(punch_data->client_secret) - 1);
    }
    if (g_config.password != NULL) {
        strncpy(punch_data->password, g_config.password, sizeof(punch_data->password) - 1);
    }
    if (g_config.type != NULL) {
        strncpy(punch_data->type, g_config.type, sizeof(punch_data->type) - 1);
    }
    pthread_mutex_unlock(&g_config_mutex);

    /* Submit in background thread */
    pthread_t thread;
    int ret = pthread_create(&thread, NULL, submit_attendance_thread, punch_data);
    if (ret != 0) {
        fprintf(stderr, "Error creating thread: %d\n", ret);
        show_error_async("Failed to create background thread");
        free(punch_data);
        return;
    }
    pthread_detach(thread);
}

/**
 * Handler for window destroy event
 */
static void on_window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;  /* Unused */
    (void)data;    /* Unused */
    
    /* Stop timer if running */
    if (g_timer_id != 0) {
        g_source_remove(g_timer_id);
        g_timer_id = 0;
    }
    
    gtk_main_quit();
}

/**
 * Create and show the main overlay window
 */
static void create_overlay_window(void) {
    GtkWidget *box;
    GtkWidget *button_box;
    GtkWidget *start_button, *stop_button;
    
    gtk_init(NULL, NULL);
    
    /* Create main window */
    g_main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    set_window_icon(GTK_WINDOW(g_main_window), APP_ICON_PATH);
    gtk_window_set_title(GTK_WINDOW(g_main_window), "cOrange Timer");
    gtk_window_set_default_size(GTK_WINDOW(g_main_window), 250, 60);
    gtk_window_set_keep_above(GTK_WINDOW(g_main_window), TRUE);

    g_signal_connect(g_main_window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    
    /* Create layout */
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(g_main_window), box);
    
    /* Create time display label */
    g_time_label = gtk_label_new("00:00:00");
    gtk_box_pack_start(GTK_BOX(box), g_time_label, TRUE, TRUE, 0);

    /* Style the label */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, 
        "label { font-weight: bold; font-size: 24px; color: #2e7d32; background-color: #f5f5f5; padding: 10px; }", 
        -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(g_time_label), 
                                   GTK_STYLE_PROVIDER(provider), 
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
    
    /* Create button container */
    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(box), button_box, TRUE, TRUE, 0);
    
    /* Create buttons */
    start_button = gtk_button_new_with_label("Start");
    stop_button = gtk_button_new_with_label("Stop");
    gtk_widget_set_sensitive(stop_button, FALSE);
    
    gtk_box_pack_start(GTK_BOX(button_box), start_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), stop_button, TRUE, TRUE, 0);
    
    /* Connect signals */
    g_signal_connect(start_button, "clicked", G_CALLBACK(on_start_button_clicked), stop_button);
    g_signal_connect(stop_button, "clicked", G_CALLBACK(on_stop_button_clicked), start_button);
    
    gtk_widget_show_all(g_main_window);
}

int main(int argc, char *argv[]) {
    (void)argc;  /* Unused */
    (void)argv;  /* Unused */
    
    /* Initialize CURL globally */
    if (orangehrm_client_init() != 0) {
        fprintf(stderr, "Failed to initialize OrangeHRM client\n");
        return -1;
    }
    
    /* Load configuration */
    pthread_mutex_lock(&g_config_mutex);
    int config_result = load_config(&g_config);
    pthread_mutex_unlock(&g_config_mutex);
    
    if (config_result != 0) {
        fprintf(stderr, "Failed to load configuration. Please check config.json\n");
        orangehrm_client_cleanup();
        return -1;
    }

    /* Create and show window */
    create_overlay_window();
    
    /* Run GTK main loop */
    gtk_main();
    
    /* Cleanup */
    pthread_mutex_lock(&g_config_mutex);
    config_free(&g_config);
    pthread_mutex_unlock(&g_config_mutex);
    
    pthread_mutex_destroy(&g_config_mutex);
    orangehrm_client_cleanup();

    return 0;
}
