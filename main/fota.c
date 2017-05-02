
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_request.h"

#define APP_VERSION     "1.0"
#define WIFI_SSID       CONFIG_WIFI_SSID
#define WIFI_PASS       CONFIG_WIFI_PASSWORD
#define APIKEY          CONFIG_APIKEY
#define SERVER_ENDPOINT "http://fota.vn/api/fota/%s"

static const char *TAG = "FOTA";
uint8_t sta_mac[6];
/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wait_for_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);

    ESP_LOGI(TAG, "Wait for WiFi....");

    ESP_LOGI(TAG,"+---------------------------------+");
    ESP_LOGE(TAG,"| APP Version =  %s              |", APP_VERSION);
    ESP_LOGI(TAG,"| DeviceId =  %02X:%02X:%02X:%02X:%02X:%02X   |", sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
    ESP_LOGI(TAG,"| API Key =  %s            |", APIKEY);
    ESP_LOGI(TAG,"+---------------------------------+");
    vTaskDelay(5000/portTICK_PERIOD_MS);
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP, freemem=%d", esp_get_free_heap_size());
}


int download_callback(request_t *req, char *data, int len)
{
    list_t *found = req->response->header;
    static int binary_len = -1, remain_len = -1;
    static esp_ota_handle_t update_handle = 0;
    static const esp_partition_t *update_partition = NULL;
    esp_err_t err;
    ESP_LOGI(TAG, "downloading...%d/%d bytes, remain=%d bytes", len, binary_len, remain_len);
    if(req->response->status_code == 200) {
        //first time
        if(binary_len == -1) {
            found = list_get_key(req->response->header, "Content-Length");
            if(found) {
                ESP_LOGI(TAG, "Binary len=%s", (char*)found->value);
                binary_len = atoi(found->value);
                remain_len = binary_len;
            } else {
                ESP_LOGE(TAG, "Erorr get connent length");
                return -1;
            }
            update_partition = esp_ota_get_next_update_partition(NULL);
            ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
                     update_partition->subtype, update_partition->address);
            assert(update_partition != NULL);

            err = esp_ota_begin(update_partition, binary_len, &update_handle);
            if(err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
                return -1;
            }
            ESP_LOGI(TAG, "esp_ota_begin succeeded");
            ESP_LOGI(TAG, "downloading..., total=%d bytes", binary_len);
        }
        err = esp_ota_write(update_handle, (const void *)data, len);
        if(err != ESP_OK) {
            ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%x", err);
            return -1;
        }
        remain_len -= len;

        //finish
        if(remain_len == 0) {
            if(esp_ota_end(update_handle) != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_end failed!");
                return -1;
            }
            err = esp_ota_set_boot_partition(update_partition);
            if(err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%x", err);
                return -1;
            }
            ESP_LOGI(TAG, "Prepare to restart system!");
            esp_restart();
        }
        return 0;
    }
    return -1;
}

static void ota_task(void *pvParameter)
{
    request_t *req;

    char data[64];

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    assert(configured == running); /* fresh from reset, should be running from configured boot partition */
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             configured->type, configured->subtype, configured->address);

   

    sprintf(data, SERVER_ENDPOINT, APIKEY);
    req = req_new(data);
    req_setopt(req, REQ_SET_METHOD, "GET");

    
    sprintf(data, "x-esp32-sta-mac:%02X:%02X:%02X:%02X:%02X:%02X", sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
    req_setopt(req, REQ_SET_HEADER, data);

    sprintf(data, "x-esp32-version:%s", APP_VERSION);
    req_setopt(req, REQ_SET_HEADER, data);

    req_setopt(req, REQ_FUNC_DOWNLOAD_CB, download_callback);
    req_perform(req);
    req_clean(req); 

    ESP_LOGE(TAG, "Goes here without reset? error was happen or now new firmware");
    vTaskDelete(NULL);
}


void app_main()
{
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        const esp_partition_t* nvs_partition = esp_partition_find_first(
                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
        assert(nvs_partition && "partition table must have an NVS partition");
        ESP_ERROR_CHECK(esp_partition_erase_range(nvs_partition, 0, nvs_partition->size));
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    
    wait_for_wifi();
    xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
}
