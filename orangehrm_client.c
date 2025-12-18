#include "orangehrm_client.h"
#include <curl/curl.h>
#include <json-c/json.h>

#define TOKEN_URL "/oauth/issueToken"
#define API_URL "/api/v1/"

size_t write_callback(void *ptr, size_t size, size_t nmemb, char *data) {
    strcat(data, ptr);  // Append the received data to the response buffer
    return size * nmemb;
}

// Function to load configuration from config.json
int load_config(Config *config) {
    FILE *file = fopen(CONFIG_FILE, "r");
    if (!file) {
        perror("Failed to open config file");
        return -1;
    }

    char buffer[1024];
    fread(buffer, 1, sizeof(buffer), file);
    fclose(file);

    struct json_object *parsed_json = json_tokener_parse(buffer);
    config->base_url = strdup(json_object_get_string(json_object_object_get(parsed_json, "base_url")));
    config->username = strdup(json_object_get_string(json_object_object_get(parsed_json, "username")));
    config->password = strdup(json_object_get_string(json_object_object_get(parsed_json, "password")));
    config->client_id = strdup(json_object_get_string(json_object_object_get(parsed_json, "client_id")));
    config->client_secret = strdup(json_object_get_string(json_object_object_get(parsed_json, "client_secret")));
    config->type = strdup(json_object_get_string(json_object_object_get(parsed_json, "type")));
    
    return 0;
}

// Function to obtain an access token using client credentials
int get_token(Config *config) {
    char post_data[1024];
    char response_data[1024*10];
    // Check which type of grant type to use
    if (strcmp(config->type, "client_credentials") == 0) {
        // Use client_credentials
        snprintf(post_data, sizeof(post_data), 
                 "grant_type=client_credentials&client_id=%s&client_secret=%s", 
                 config->client_id, config->client_secret);
    } 
    else if (strcmp(config->type, "password") == 0) {
        // Use password grant type
        snprintf(post_data, sizeof(post_data), 
                 "grant_type=password&username=%s&password=%s&client_id=%s&client_secret=%s", 
                 config->username, config->password, config->client_id, config->client_secret);
    } 
    else {
        // Invalid grant type
        fprintf(stderr, "Invalid grant type: %s\n", config->type);
        return -1;
    }

    // Debugging: Print request data
    //printf("Sending POST request to: %s\n", TOKEN_URL);
    //printf("POST Data: %s\n", post_data);

    // Prepare headers for the POST request
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    // Initialize CURL
    CURL *curl = curl_easy_init();
    if (curl) {
        CURLcode res;

        // Set the URL for the request
        char full_url[512];
        snprintf(full_url, sizeof(full_url), "%s%s", config->base_url, TOKEN_URL);

        curl_easy_setopt(curl, CURLOPT_URL, full_url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_data);

        // Perform the request
        res = curl_easy_perform(curl);

        // Handle response
        if (res != CURLE_OK) {
            fprintf(stderr, "POST request failed: %s\n", curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            return -1;
        }

        // Clean up
        curl_easy_cleanup(curl);
    }
    curl_slist_free_all(headers);
    struct json_object *parsed_json = json_tokener_parse(response_data);
    if (parsed_json == NULL) {
        fprintf(stderr, "Error parsing JSON response\n");
        return -1;
    }

    struct json_object *access_token_obj;
    if (!json_object_object_get_ex(parsed_json, "access_token", &access_token_obj)) {
        fprintf(stderr, "Error: access_token not found in response\n");
        return -1;
    }

    // Extract the access token as a string
    const char *access_token = json_object_get_string(access_token_obj);
    //printf("Access Token: %s\n", access_token);

    // Store the access_token in the config struct
    config->access_token = strdup(access_token);

    // Clean up JSON object
    json_object_put(parsed_json);
    

    return 0;
}


// Function to send POST requests
int post_request(const char *url, const char *data, Config *config, char *response_data) {
       return api_request(url, "POST", data, config, response_data);
}

// Function to send GET requests
int get_request(const char *url, Config *config, char *response_data) {
    return api_request(url, "GET", NULL, config, response_data);
}

// Function to send PUT requests
int put_request(const char *url, const char *data, Config *config, char *response_data) {
    return api_request(url, "PUT", data, config, response_data);
}

// Function to send PATCH requests
int patch_request(const char *url, const char *data, Config *config, char *response_data) {
    return api_request(url, "PATCH", data, config, response_data);
}

// Function to send DELETE requests
int delete_request(const char *url, Config *config, char *response_data) {
    return api_request(url, "DELETE", NULL, config, response_data);
}

// General function for sending requests
int api_request(const char *url, const char *method, const char *data, Config *config, char *response_data) {
    CURL *curl;
    CURLcode res;
    char full_url[512];
    snprintf(full_url, sizeof(full_url), "%s%s", config->base_url, url);

    // Initialize response data buffer
    //memset(response_data, 0, MAX_RESPONSE_SIZE);  // Clear the response buffer

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if(curl) {
        struct curl_slist *headers = NULL;
        char auth_header[1024];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->access_token);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(curl, CURLOPT_URL, full_url);
        

        // Set the callback for capturing response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_data);  // Store response in response_data

        if (data != NULL) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
        }

        if (strcmp(method, "GET") == 0) {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
        } else if (strcmp(method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1);
        } else if (strcmp(method, "PUT") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        } else if (strcmp(method, "PATCH") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        } else if (strcmp(method, "DELETE") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Perform the request
        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            fprintf(stderr, "%s request failed: %s\n", method, curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            return -1;
        }

        // Clean up
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    return 0;
}
