#include "orangehrm_client.h"
#include <curl/curl.h>
#include <json-c/json.h>

#define TOKEN_URL "/oauth/issueToken"
#define API_URL "/api/v1/"

/* Global initialization flag */
static int g_initialized = 0;

/**
 * Initialize the OrangeHRM client
 * Must be called once at program startup before any API calls
 */
int orangehrm_client_init(void) {
    if (g_initialized) {
        return 0;  /* Already initialized */
    }
    
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_global_init failed: %s\n", curl_easy_strerror(res));
        return -1;
    }
    
    g_initialized = 1;
    return 0;
}

/**
 * Cleanup the OrangeHRM client
 * Must be called once at program shutdown
 */
void orangehrm_client_cleanup(void) {
    if (g_initialized) {
        curl_global_cleanup();
        g_initialized = 0;
    }
}

/**
 * Initialize a response buffer with given capacity
 */
int response_buffer_init(ResponseBuffer *resp, size_t capacity) {
    if (resp == NULL || capacity == 0) {
        return -1;
    }
    
    resp->buffer = (char *)calloc(capacity, sizeof(char));
    if (resp->buffer == NULL) {
        fprintf(stderr, "Failed to allocate response buffer\n");
        return -1;
    }
    
    resp->size = 0;
    resp->capacity = capacity;
    return 0;
}

/**
 * Free a response buffer
 */
void response_buffer_free(ResponseBuffer *resp) {
    if (resp != NULL) {
        free(resp->buffer);
        resp->buffer = NULL;
        resp->size = 0;
        resp->capacity = 0;
    }
}

/**
 * Safe write callback for CURL responses
 * Prevents buffer overflow by checking capacity
 */
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t realsize = size * nmemb;
    ResponseBuffer *resp = (ResponseBuffer *)userdata;
    
    if (resp == NULL || resp->buffer == NULL) {
        return 0;
    }
    
    /* Check if we have enough space (keep 1 byte for null terminator) */
    if (resp->size + realsize + 1 > resp->capacity) {
        fprintf(stderr, "Response buffer overflow prevented (size: %zu, capacity: %zu)\n", 
                resp->size + realsize, resp->capacity);
        return 0;  /* Signal error to curl */
    }
    
    memcpy(resp->buffer + resp->size, ptr, realsize);
    resp->size += realsize;
    resp->buffer[resp->size] = '\0';  /* Null terminate */
    
    return realsize;
}

/**
 * Free all memory allocated for config structure
 */
void config_free(Config *config) {
    if (config == NULL) {
        return;
    }
    
    free(config->base_url);
    free(config->username);
    free(config->password);
    free(config->client_id);
    free(config->client_secret);
    free(config->access_token);
    free(config->refresh_token);
    free(config->type);
    
    /* Zero out for safety */
    memset(config, 0, sizeof(Config));
}

/**
 * Helper function to safely get and duplicate a string from JSON
 * Returns NULL on failure
 */
static char* json_get_string_dup(struct json_object *json, const char *key, int required) {
    struct json_object *obj = NULL;
    
    if (!json_object_object_get_ex(json, key, &obj)) {
        if (required) {
            fprintf(stderr, "Missing required field '%s' in config\n", key);
        }
        return NULL;
    }
    
    const char *value = json_object_get_string(obj);
    if (value == NULL) {
        if (required) {
            fprintf(stderr, "Field '%s' is null in config\n", key);
        }
        return NULL;
    }
    
    char *dup = strdup(value);
    if (dup == NULL) {
        fprintf(stderr, "Memory allocation failed for field '%s'\n", key);
    }
    
    return dup;
}

/**
 * Load configuration from config.json file
 */
int load_config(Config *config) {
    FILE *file = NULL;
    struct json_object *parsed_json = NULL;
    int result = -1;
    
    if (config == NULL) {
        fprintf(stderr, "Config pointer is NULL\n");
        return -1;
    }
    
    /* Initialize config to zeros */
    memset(config, 0, sizeof(Config));
    
    file = fopen(CONFIG_FILE, "r");
    if (!file) {
        perror("Failed to open config file");
        return -1;
    }

    /* Read file contents */
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    file = NULL;
    
    if (bytes_read == 0) {
        fprintf(stderr, "Config file is empty or read failed\n");
        return -1;
    }

    /* Parse JSON */
    parsed_json = json_tokener_parse(buffer);
    if (parsed_json == NULL) {
        fprintf(stderr, "Failed to parse config JSON\n");
        return -1;
    }
    
    /* Extract required fields */
    config->base_url = json_get_string_dup(parsed_json, "base_url", 1);
    if (config->base_url == NULL) goto cleanup;
    
    config->client_id = json_get_string_dup(parsed_json, "client_id", 1);
    if (config->client_id == NULL) goto cleanup;
    
    config->client_secret = json_get_string_dup(parsed_json, "client_secret", 1);
    if (config->client_secret == NULL) goto cleanup;
    
    config->type = json_get_string_dup(parsed_json, "type", 1);
    if (config->type == NULL) goto cleanup;
    
    /* Extract optional fields (username/password for password grant) */
    config->username = json_get_string_dup(parsed_json, "username", 0);
    config->password = json_get_string_dup(parsed_json, "password", 0);
    
    /* Validate grant type has required fields */
    if (strcmp(config->type, "password") == 0) {
        if (config->username == NULL || config->password == NULL) {
            fprintf(stderr, "Password grant type requires 'username' and 'password' fields\n");
            goto cleanup;
        }
    }
    
    /* Initialize token fields to NULL */
    config->access_token = NULL;
    config->refresh_token = NULL;
    
    result = 0;  /* Success */

cleanup:
    if (parsed_json != NULL) {
        json_object_put(parsed_json);
    }
    
    if (result != 0) {
        config_free(config);
    }
    
    return result;
}

/**
 * Obtain an access token using OAuth2
 */
int get_token(Config *config) {
    char post_data[MAX_HEADER_SIZE];
    ResponseBuffer resp;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    int result = -1;
    
    if (config == NULL) {
        fprintf(stderr, "Config is NULL\n");
        return -1;
    }
    
    /* Initialize response buffer */
    if (response_buffer_init(&resp, MAX_RESPONSE_SIZE) != 0) {
        return -1;
    }
    
    /* Build POST data based on grant type */
    if (strcmp(config->type, "client_credentials") == 0) {
        snprintf(post_data, sizeof(post_data), 
                 "grant_type=client_credentials&client_id=%s&client_secret=%s", 
                 config->client_id, config->client_secret);
    } 
    else if (strcmp(config->type, "password") == 0) {
        snprintf(post_data, sizeof(post_data), 
                 "grant_type=password&username=%s&password=%s&client_id=%s&client_secret=%s", 
                 config->username, config->password, config->client_id, config->client_secret);
    } 
    else {
        fprintf(stderr, "Invalid grant type: %s\n", config->type);
        goto cleanup;
    }

    /* Set up headers */
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    if (headers == NULL) {
        fprintf(stderr, "Failed to create headers\n");
        goto cleanup;
    }

    /* Initialize CURL */
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize CURL\n");
        goto cleanup;
    }
    
    /* Build full URL */
    char full_url[MAX_URL_SIZE];
    snprintf(full_url, sizeof(full_url), "%s%s", config->base_url, TOKEN_URL);

    curl_easy_setopt(curl, CURLOPT_URL, full_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

    /* Perform the request */
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Token request failed: %s\n", curl_easy_strerror(res));
        goto cleanup;
    }

    /* Parse response */
    struct json_object *parsed_json = json_tokener_parse(resp.buffer);
    if (parsed_json == NULL) {
        fprintf(stderr, "Error parsing token response JSON\n");
        goto cleanup;
    }

    struct json_object *access_token_obj;
    if (!json_object_object_get_ex(parsed_json, "access_token", &access_token_obj)) {
        fprintf(stderr, "Error: access_token not found in response\n");
        json_object_put(parsed_json);
        goto cleanup;
    }

    /* Store access token */
    const char *access_token = json_object_get_string(access_token_obj);
    if (access_token == NULL) {
        fprintf(stderr, "Error: access_token is null\n");
        json_object_put(parsed_json);
        goto cleanup;
    }
    
    /* Free old token if exists */
    free(config->access_token);
    config->access_token = strdup(access_token);
    
    if (config->access_token == NULL) {
        fprintf(stderr, "Failed to allocate memory for access token\n");
        json_object_put(parsed_json);
        goto cleanup;
    }

    json_object_put(parsed_json);
    result = 0;  /* Success */

cleanup:
    if (curl) {
        curl_easy_cleanup(curl);
    }
    if (headers) {
        curl_slist_free_all(headers);
    }
    response_buffer_free(&resp);
    
    return result;
}

/* Wrapper functions for different HTTP methods */
int post_request(const char *url, const char *data, Config *config, ResponseBuffer *resp) {
    return api_request(url, "POST", data, config, resp);
}

int get_request(const char *url, Config *config, ResponseBuffer *resp) {
    return api_request(url, "GET", NULL, config, resp);
}

int put_request(const char *url, const char *data, Config *config, ResponseBuffer *resp) {
    return api_request(url, "PUT", data, config, resp);
}

int patch_request(const char *url, const char *data, Config *config, ResponseBuffer *resp) {
    return api_request(url, "PATCH", data, config, resp);
}

int delete_request(const char *url, Config *config, ResponseBuffer *resp) {
    return api_request(url, "DELETE", NULL, config, resp);
}

/**
 * General function for sending API requests
 */
int api_request(const char *url, const char *method, const char *data, Config *config, ResponseBuffer *resp) {
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    int result = -1;
    
    if (url == NULL || method == NULL || config == NULL || resp == NULL) {
        fprintf(stderr, "Invalid parameters for api_request\n");
        return -1;
    }
    
    if (config->access_token == NULL) {
        fprintf(stderr, "No access token available. Call get_token first.\n");
        return -1;
    }
    
    /* Reset response buffer */
    resp->size = 0;
    if (resp->buffer != NULL) {
        resp->buffer[0] = '\0';
    }
    
    /* Build full URL */
    char full_url[MAX_URL_SIZE];
    snprintf(full_url, sizeof(full_url), "%s%s", config->base_url, url);

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize CURL\n");
        return -1;
    }

    /* Set up authorization header */
    char auth_header[MAX_HEADER_SIZE];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->access_token);
    headers = curl_slist_append(headers, auth_header);
    
    if (headers == NULL) {
        fprintf(stderr, "Failed to create headers\n");
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_URL, full_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);

    /* Set request body if provided */
    if (data != NULL) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }

    /* Set HTTP method */
    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (strcmp(method, "PATCH") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
        fprintf(stderr, "Unknown HTTP method: %s\n", method);
        goto cleanup;
    }
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* Perform the request */
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "%s request failed: %s\n", method, curl_easy_strerror(res));
        goto cleanup;
    }

    result = 0;  /* Success */

cleanup:
    if (curl) {
        curl_easy_cleanup(curl);
    }
    if (headers) {
        curl_slist_free_all(headers);
    }

    return result;
}
