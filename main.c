#include "orangehrm_client.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

time_t start_time, stop_time;
guint timer_id; // Timer identifier
GtkWidget *time_label;
Config config;

void write_log(const char *message) {
    FILE *log_file = fopen("application.log", "a");
    if (log_file == NULL) {
        fprintf(stderr, "Error opening log file\n");
        return;
    }
    time_t current_time;
    struct tm *tm_info;
    char time_str[20];  // Format: YYYY-MM-DD HH:MM:SS
    time(&current_time); // Get current time
    tm_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log_file, "[%s] %s\n", time_str, message);
    fclose(log_file);
}

void format_time(time_t raw_time, char *formatted_day, char *formatted_time, char *time_zone) {
    struct tm *tm_info;
    int timezone_offset_in_minutes;
    tm_info = localtime(&raw_time);
    timezone_offset_in_minutes = tm_info->tm_gmtoff / 60;
    strftime(formatted_day, 100, "%Y-%m-%d", tm_info);
    strftime(formatted_time, 100, "%H:%M", tm_info);
    float hours_offset = timezone_offset_in_minutes / 60.0;
    snprintf(time_zone, 10, " %+05.1f", hours_offset);
}

void format_elapsed_time(double elapsed_time, char *formatted_time) {
    int hours = (int)(elapsed_time / 3600);
    int minutes = (int)((elapsed_time - (hours * 3600)) / 60);
    int seconds = (int)(elapsed_time - (hours * 3600) - (minutes * 60));
    snprintf(formatted_time, 100, "%02d:%02d:%02d", hours, minutes, seconds);
}

void* print_start_end_time(void *data) {
    time_t *times = (time_t*)data;
    time_t start_time = times[0];
    time_t stop_time = times[1];
    
    char formatted_start_day[100], formatted_start_time[100], start_time_zone[10];
    char formatted_end_day[100], formatted_end_time[100], end_time_zone[10];

    format_time(start_time, formatted_start_day, formatted_start_time, start_time_zone);
    format_time(stop_time, formatted_end_day, formatted_end_time, end_time_zone);

    struct json_object *json_object = json_object_new_object();
    json_object_object_add(json_object, "empNumber", json_object_new_string(config.username));
    json_object_object_add(json_object, "punchInDate", json_object_new_string(formatted_start_day));
    json_object_object_add(json_object, "punchInTime", json_object_new_string(formatted_start_time));
    json_object_object_add(json_object, "punchInTimezoneOffset", json_object_new_string(start_time_zone));
    json_object_object_add(json_object, "punchInNote", json_object_new_string("App In"));
    json_object_object_add(json_object, "punchOutDate", json_object_new_string(formatted_end_day));
    json_object_object_add(json_object, "punchOutTime", json_object_new_string(formatted_end_time));
    json_object_object_add(json_object, "punchOutTimezoneOffset", json_object_new_string(end_time_zone));
    json_object_object_add(json_object, "punchOutNote", json_object_new_string("App out"));

    const char *json_string = json_object_to_json_string_ext(json_object, JSON_C_TO_STRING_PRETTY);
    printf("%s\n", json_string);
    
    int sucuess = 1;
    if (get_token(&config) != 0) {
        printf("Failed to obtain access token.\n"); 
        sucuess = 0;
    }

    char response_data[1024*1000];
    if(sucuess == 1){
        if (post_request("/api/attendanceRecords", json_string, &config, response_data) != 0) {
            printf("POST request failed.\n");
            sucuess = 0;
        }
    }

    struct json_object *parsed_json = json_tokener_parse(response_data);
    if (parsed_json == NULL) {
        fprintf(stderr, "Error parsing JSON response\n");
        sucuess = 0;
    }

    struct json_object *success_obj;
    if (!json_object_object_get_ex(parsed_json, "success", &success_obj)) {
        fprintf(stderr, "Error: success not found in response\n");
        sucuess = 0;
    }

    const char *success_token = json_object_get_string(success_obj);
    if (strcmp(success_token, "false") == 0) {
        sucuess = 0;
    } else {
        sucuess = 1;
    }

    if(sucuess == 0){
        write_log(json_string);
        write_log(response_data);
    }

    json_object_put(json_object);
    free(times);
    pthread_exit(NULL);
}

gboolean update_time_label(gpointer data) {
    time_t current_time;
    double elapsed_time;
    time(&current_time);
    
    if (start_time != 0) {
        elapsed_time = difftime(current_time, start_time);
    } else {
        elapsed_time = 0;
    }

    char formatted_time[100];
    format_elapsed_time(elapsed_time, formatted_time);
    gtk_label_set_text(GTK_LABEL(time_label), formatted_time);
    
    return TRUE;
}

void on_start_button_clicked(GtkWidget *widget, gpointer data) {
    gtk_widget_set_sensitive(widget, FALSE); // Disable the start button
    GtkWidget *stop_button = GTK_WIDGET(data);
    gtk_widget_set_sensitive(stop_button, TRUE); // Enable the stop button
    time(&start_time);
    timer_id = g_timeout_add(1000, update_time_label, NULL);  // 1000 ms = 1 second
}

void on_stop_button_clicked(GtkWidget *widget, gpointer data) {
    if (timer_id != 0) {
        g_source_remove(timer_id);  // Stop the timer
        timer_id = 0;
    }

    time(&stop_time);
    double elapsed_time = difftime(stop_time, start_time);
    char formatted_time[100];
    format_elapsed_time(elapsed_time, formatted_time);
    gtk_label_set_text(GTK_LABEL(time_label), formatted_time);

    GtkWidget *start_button = GTK_WIDGET(data);  // Retrieve the start button
    gtk_widget_set_sensitive(start_button, TRUE);  // Re-enable the start button
    gtk_widget_set_sensitive(widget, FALSE);
    
    time_t *times = (time_t*)malloc(2 * sizeof(time_t));
    times[0] = start_time;
    times[1] = stop_time;

    pthread_t thread;
    int ret = pthread_create(&thread, NULL, print_start_end_time, (void*)times);
    if (ret != 0) {
        printf("Error creating thread\n");
    }
    pthread_detach(thread);  // Detach the thread so it cleans up after itself
}

void create_overlay_window() {
    GtkWidget *window;
    GtkWidget *box;
    GtkWidget *button_box;
    GtkWidget *start_button, *stop_button;
    
    gtk_init(NULL, NULL);
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "cOrange Timer");
    gtk_window_set_default_size(GTK_WINDOW(window), 250, 60); // Smaller window size
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), box);
    
    time_label = gtk_label_new("00:00:00");
    gtk_box_pack_start(GTK_BOX(box), time_label, TRUE, TRUE, 0);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, 
        "label { font-weight: bold; color: green; background-color: white; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(time_label), 
                                   GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
    
    button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);  // Horizontal box for buttons
    gtk_box_pack_start(GTK_BOX(box), button_box, TRUE, TRUE, 0);
    
    start_button = gtk_button_new_with_label("Start");
    stop_button = gtk_button_new_with_label("Stop");

    gtk_widget_set_sensitive(stop_button, FALSE); // Disable stop button initially
    
    gtk_box_pack_start(GTK_BOX(button_box), start_button, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), stop_button, TRUE, TRUE, 0);
    
    g_signal_connect(start_button, "clicked", G_CALLBACK(on_start_button_clicked), stop_button);
    g_signal_connect(stop_button, "clicked", G_CALLBACK(on_stop_button_clicked), start_button);
    
     gtk_widget_set_sensitive(stop_button, FALSE);
    
    gtk_widget_show_all(window);
}

int main(int argc, char *argv[]) {
    create_overlay_window();

    if (load_config(&config) != 0) {
        printf("Failed to load configuration.\n");
        return -1;
    }

    gtk_main();
    
    free(config.base_url);
    free(config.username);
    free(config.password);
    free(config.client_id);
    free(config.client_secret);

    return 0;
}
