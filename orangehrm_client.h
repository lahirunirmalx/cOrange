#ifndef ORANGEHRM_CLIENT_H
#define ORANGEHRM_CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define CONFIG_FILE "config.json"

typedef struct {
    char *base_url;
    char *username;
    char *password;
    char *client_id;
    char *client_secret;
    char *access_token;
    char *refresh_token;
    char *type;
} Config;

// Functions to interact with the API
int load_config(Config *config);
int get_token(Config *config);
int api_request(const char *url, const char *method, const char *data, Config *config,char *response_data);
int post_request(const char *url, const char *data, Config *config,char *response_data);
int get_request(const char *url, Config *config,char *response_data);
int put_request(const char *url, const char *data, Config *config,char *response_data);
int patch_request(const char *url, const char *data, Config *config,char *response_data);
int delete_request(const char *url, Config *config,char *response_data);
size_t write_callback(void *ptr, size_t size, size_t nmemb, char *data);

#endif // ORANGEHRM_CLIENT_H
