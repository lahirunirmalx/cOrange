#ifndef ORANGEHRM_CLIENT_H
#define ORANGEHRM_CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define CONFIG_FILE "config.json"
#define MAX_RESPONSE_SIZE (1024 * 100)  /* 100KB response buffer */
#define MAX_URL_SIZE 512
#define MAX_HEADER_SIZE 1024

/**
 * Configuration structure for OrangeHRM API client
 */
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

/**
 * Response buffer structure for safe data handling
 */
typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} ResponseBuffer;

/**
 * Initialize the OrangeHRM client (call once at startup)
 * @return 0 on success, -1 on failure
 */
int orangehrm_client_init(void);

/**
 * Cleanup the OrangeHRM client (call once at shutdown)
 */
void orangehrm_client_cleanup(void);

/**
 * Initialize a response buffer
 * @param resp Pointer to ResponseBuffer structure
 * @param capacity Initial capacity in bytes
 * @return 0 on success, -1 on failure
 */
int response_buffer_init(ResponseBuffer *resp, size_t capacity);

/**
 * Free a response buffer
 * @param resp Pointer to ResponseBuffer structure
 */
void response_buffer_free(ResponseBuffer *resp);

/**
 * Load configuration from config.json file
 * @param config Pointer to Config structure to populate
 * @return 0 on success, -1 on failure
 */
int load_config(Config *config);

/**
 * Free all memory allocated for config
 * @param config Pointer to Config structure to free
 */
void config_free(Config *config);

/**
 * Obtain an access token using OAuth2
 * @param config Pointer to Config structure (token stored here)
 * @return 0 on success, -1 on failure
 */
int get_token(Config *config);

/**
 * General API request function
 * @param url API endpoint (will be appended to base_url)
 * @param method HTTP method (GET, POST, PUT, PATCH, DELETE)
 * @param data Request body data (can be NULL for GET/DELETE)
 * @param config Pointer to Config structure with credentials
 * @param resp Pointer to ResponseBuffer to store response
 * @return 0 on success, -1 on failure
 */
int api_request(const char *url, const char *method, const char *data, Config *config, ResponseBuffer *resp);

/**
 * Send a POST request
 */
int post_request(const char *url, const char *data, Config *config, ResponseBuffer *resp);

/**
 * Send a GET request
 */
int get_request(const char *url, Config *config, ResponseBuffer *resp);

/**
 * Send a PUT request
 */
int put_request(const char *url, const char *data, Config *config, ResponseBuffer *resp);

/**
 * Send a PATCH request
 */
int patch_request(const char *url, const char *data, Config *config, ResponseBuffer *resp);

/**
 * Send a DELETE request
 */
int delete_request(const char *url, Config *config, ResponseBuffer *resp);

/**
 * CURL write callback function (internal use)
 */
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata);

#endif /* ORANGEHRM_CLIENT_H */
