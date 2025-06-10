#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <arpa/inet.h>
#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "esp_http_server.h"
#include "cJSON.h"

#define MAX_FILEPATH_LEN 256
#define EXAMPLE_ESP_WIFI_SSID "Routines.dev"
#define EXAMPLE_ESP_WIFI_PASS "Test1234"
#define EXAMPLE_MAX_STA_CONN 30
#define STATIC_IP_ADDR "10.0.10.1"

static const char *TAG = "wifi_softAP";

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#define PIN_NUM_MISO  5
#define PIN_NUM_MOSI  6
#define PIN_NUM_CLK   7
#define PIN_NUM_CS    4
#define MOUNT_POINT   "/sd"

static sdmmc_card_t *card;

void cjson_flatten_array(cJSON *array) {
    if (!array || !cJSON_IsArray(array)) return;

    int i = 0;
    while (i < cJSON_GetArraySize(array)) {
        cJSON *item = cJSON_GetArrayItem(array, i);

        if (cJSON_IsArray(item)) {
            int sub_size = cJSON_GetArraySize(item);

            for (int j = 0; j < sub_size; j++) {
                cJSON *sub_item = cJSON_DetachItemFromArray(item, 0);
                cJSON_AddItemToArray(array, sub_item);
            }

            cJSON_DetachItemFromArray(array, i);
            cJSON_Delete(item);

            continue;
        }
        i++;
    }
}


void list_sdcard_files(void)
{
    DIR *dir = opendir(MOUNT_POINT);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open dir: %s", MOUNT_POINT);
        return;
    }
    ESP_LOGI(TAG, "Contents of %s:", MOUNT_POINT);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }
    closedir(dir);
}

esp_err_t mount_sdcard(void)
{
    ESP_LOGI(TAG, ">> mount_sdcard()");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000
    };
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 10,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Mounting SD card at %s ...", MOUNT_POINT);
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FS (%s)", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted");
    return ESP_OK;
}

static esp_err_t serve_file(httpd_req_t *req, const char *filepath, const char *content_type)
{
    FILE *file = fopen(filepath, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s, errno: %d (%s)", filepath, errno, strerror(errno));
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        list_sdcard_files();
        return ESP_FAIL;
    }

    char chunk[1024];
    size_t read_bytes;

    httpd_resp_set_type(req, content_type);

    while ((read_bytes = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    return serve_file(req, "/sd/Dashboard.html", "text/html");
}

static esp_err_t static_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Requested URI: %s", req->uri);

    char filepath[MAX_FILEPATH_LEN] = "/sd";
    size_t uri_len = strlen(req->uri);
    size_t base_len = strlen(filepath);

    if (base_len + uri_len >= sizeof(filepath)) {
        ESP_LOGE(TAG, "Filepath buffer too small, truncation would occur");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal server error");
        return ESP_FAIL;
    }

    strlcat(filepath, req->uri, sizeof(filepath));

    char *query = strchr(filepath, '?');
    if (query) *query = '\0';

    ESP_LOGI(TAG, "Attempting to serve file: %s", filepath);

    const char *content_type = "application/octet-stream";
    if (strstr(req->uri, ".js")) {
        content_type = "application/javascript";
    } else if (strstr(req->uri, ".css")) {
        content_type = "text/css";
    } else if (strstr(req->uri, ".html")) {
        content_type = "text/html";
    } else if (strstr(req->uri, ".ico")) {
        content_type = "image/x-icon";
    }

    return serve_file(req, filepath, content_type);
}

esp_err_t login_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    ESP_LOGI("login", "Received POST: %s", buf);

    // Parse JSON
    cJSON *root = cJSON_Parse(buf);
    if (!root) return ESP_FAIL;

    cJSON *username_item = cJSON_GetObjectItem(root, "username");
    cJSON *password_item = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(username_item) || !cJSON_IsString(password_item)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    const char *username = username_item->valuestring;
    const char *password = password_item->valuestring;

    ESP_LOGI("login", "Username: %s", username);
    ESP_LOGI("login", "Password: %s", password);

    FILE *f = fopen("/sd/Users/creds.json", "r");
    if (!f) {
        ESP_LOGE("login", "Failed to open creds.json");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *creds_buf = malloc(sz + 1);
    if (!creds_buf) {
        fclose(f);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    fread(creds_buf, 1, sz, f);
    creds_buf[sz] = '\0';
    fclose(f);

    cJSON *creds_root = cJSON_Parse(creds_buf);
    free(creds_buf);
    if (!creds_root) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    bool valid = false;
    cJSON *users = cJSON_GetObjectItem(creds_root, "users");
    cJSON *entry;
    cJSON_ArrayForEach(entry, users) {
        cJSON *u = cJSON_GetObjectItem(entry, "username");
        cJSON *p = cJSON_GetObjectItem(entry, "password");
        if (cJSON_IsString(u) && cJSON_IsString(p) &&
            strcmp(u->valuestring, username) == 0 &&
            strcmp(p->valuestring, password) == 0) {
            valid = true;
            break;
        }
    }

    cJSON_Delete(creds_root);
    cJSON_Delete(root);

    if (!valid) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
        return ESP_OK;
    }

    char cookie[128];
    snprintf(cookie, sizeof(cookie),
             "userToken=%s; Path=/; Max-Age=604800; SameSite=Lax; HttpOnly",
             username);

    ESP_LOGI("login", "Setting cookie: %s", cookie);

    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "http://10.0.10.1");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, username, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}


esp_err_t save_post_handler(httpd_req_t *req) {
    char token[64] = {0};
    char *query = strchr(req->uri, '?');
    if (query) {
        httpd_query_key_value(query + 1, "token", token, sizeof(token));
    }
    if (strlen(token) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing token");
        return ESP_FAIL;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sd/Data/user_%s.json", token);

    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory alloc failed");
        return ESP_FAIL;
    }

    int remaining = req->content_len, received = 0;
    while (remaining > 0) {
        int ret = httpd_req_recv(req, buf + received, remaining);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        received += ret;
        remaining -= ret;
    }
    buf[received] = '\0';
    ESP_LOGW(TAG, "POST received body: %s", buf);

    FILE *f = fopen(filepath, "r");
    char *existing_content = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        rewind(f);
        existing_content = malloc(fsize + 1);
        if (existing_content) {
            fread(existing_content, 1, fsize, f);
            existing_content[fsize] = '\0';
        }
        fclose(f);
    }

    cJSON *json_array = NULL;
    ESP_LOGI(TAG, "Parsed existing JSON array.");
    if (existing_content && strlen(existing_content) > 0) {
        json_array = cJSON_Parse(existing_content);
        free(existing_content);
        if (!json_array || !cJSON_IsArray(json_array)) {
            if (json_array) cJSON_Delete(json_array);
            json_array = cJSON_CreateArray();
        }
    } else {
        json_array = cJSON_CreateArray();
    }

    cJSON *new_task = cJSON_Parse(buf);
    ESP_LOGI(TAG, "Parsed existing JSON array.");
    free(buf);
    if (!new_task) {
        cJSON_Delete(json_array);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON_AddItemToArray(json_array, new_task);
    ESP_LOGI(TAG, "Appending new object.");

    cjson_flatten_array(json_array);

    char *updated_json = cJSON_PrintUnformatted(json_array);
    cJSON_Delete(json_array);
    if (!updated_json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize JSON");
        return ESP_FAIL;
    }

    f = fopen(filepath, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s (errno %d)", strerror(errno), errno);
        free(updated_json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }


    size_t written = fwrite(updated_json, 1, strlen(updated_json), f);
    fsync(fileno(f));
    fclose(f);


    if (written != strlen(updated_json)) {
        ESP_LOGE(TAG, "Write failed: expected %d, wrote %d", strlen(updated_json), written);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file");
        return ESP_FAIL;
    }

    free(updated_json);

    httpd_resp_sendstr(req, "Data saved");
    
    return ESP_OK;
}

esp_err_t load_get_handler(httpd_req_t *req) {
    char token[64] = {0};

    char *query = strchr(req->uri, '?');
    if (query) {
        httpd_query_key_value(query + 1, "token", token, sizeof(token));
    }

    if (strlen(token) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing token");
        return ESP_FAIL;
    }

    char filepath[128];
    snprintf(filepath, sizeof(filepath), "/sd/Data/user_%s.json", token);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGW(TAG, "File not found: %s, creating new one", filepath);

        f = fopen(filepath, "w");
        if (!f) {
            ESP_LOGE(TAG, "Failed to create file: %s", filepath);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
            return ESP_FAIL;
        }

        const char *initial_content = "[]";
        fwrite(initial_content, 1, strlen(initial_content), f);
        fclose(f);

        f = fopen(filepath, "r");
        if (!f) {
            ESP_LOGE(TAG, "Failed to reopen file: %s", filepath);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File access error");
            return ESP_FAIL;
        }
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    char *data = malloc(fsize + 1);
    fread(data, 1, fsize, f);
    data[fsize] = '\0';
    fclose(f);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, data, fsize);
    free(data);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {

        httpd_uri_t save_handler = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &save_handler);

        httpd_uri_t load_handler = {
            .uri = "/load",
            .method = HTTP_GET,
            .handler = load_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &load_handler);

        httpd_uri_t login = {
            .uri = "/login",
            .method = HTTP_POST,
            .handler = login_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &login);

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t static_files = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = static_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &static_files);
        }
    return server;
}

void start_filesystem()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = false
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount or format filesystem (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS info (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
    }
}

void wifi_init_softap()
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    esp_netif_dhcps_stop(ap_netif);

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 10, 0, 10, 1);
    IP4_ADDR(&ip_info.gw, 10, 0, 10, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(ap_netif, &ip_info);

    esp_netif_dhcps_start(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = 1,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "SoftAP configured with IP: %s", STATIC_IP_ADDR);
}

void app_main(void)
{
    nvs_flash_init();
    start_filesystem();
    mount_sdcard();
    wifi_init_softap();
    start_webserver();
}
