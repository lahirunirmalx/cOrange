#include "orangehrm_client.h"

int main() {
    Config config;

    if (load_config(&config) != 0) {
        printf("Failed to load configuration.\n");
        return -1;
    }

    // Get access token
    if (get_token(&config) != 0) {
        printf("Failed to obtain access token.\n");
        return -1;
    }

    // Example API call
    char response_data[1024*1000];
    if (get_request("/api/employees", &config, response_data) != 0) {
        printf("GET request failed.\n");
    }
  printf("Response Data: %s\n", response_data);

    // Cleanup
    free(config.base_url);
    free(config.username);
    free(config.password);
    free(config.client_id);
    free(config.client_secret);

    return 0;
}
