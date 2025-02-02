#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "qrcode.h"

// Logging tag
#define TAG "PROVISIONER"

// Constants for provisioning
#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_BLE "ble"
#define QRCODE_BASE_URL "https://espressif.github.io/esp-jumpstart/qrcode.html"
#define BUTTON_GPIO GPIO_NUM_32  // GPIO pin for the button
#define DEBOUNCE_MS 50  // Debounce time for button presses

static EventGroupHandle_t wifi_event_group;  // Event group handle for Wi-Fi events
static QueueHandle_t button_queue = NULL;  // Queue handle for button press events
static char service_name[12];  // Service name for provisioning
static const char *pop = "abcd1234";  // Proof-of-possession (PoP) password

// Event handler for Wi-Fi and provisioning events
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received SSID: %s, Password: %s",
                         cfg->ssid, cfg->password);
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning stopped");
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();  // Attempt to connect to the Wi-Fi network
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

// Generate a unique service name based on the device's MAC address
static void get_device_service_name(char *name, size_t max) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(name, max, "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

// Start the Wi-Fi provisioning process
static void start_provisioning() {
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, service_name, NULL));
    
    char payload[150];
    snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
             PROV_QR_VERSION, service_name, pop, PROV_TRANSPORT_BLE);
    
    ESP_LOGI(TAG, "Scan this QR code for provisioning:");
    esp_qrcode_config_t qr_config = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&qr_config, payload);
}

// Stop the Wi-Fi provisioning process
static void stop_provisioning() {
    wifi_prov_mgr_stop_provisioning();
    wifi_prov_mgr_reset_provisioning();
    wifi_prov_mgr_deinit();
}

// Interrupt handler for button press
static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(button_queue, &gpio_num, NULL);
}

// Task to handle button presses
static void button_task(void *arg) {
    uint32_t io_num;
    while (1) {
        if (xQueueReceive(button_queue, &io_num, portMAX_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));  // Debounce delay
            if (gpio_get_level(io_num)) {
                ESP_LOGI(TAG, "Triggering reprovisioning");
                esp_wifi_disconnect();
                stop_provisioning();
                start_provisioning();
            }
        }
    }
}

// Main application entry point
void app_main() {
    // Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Initialize network interface and event loop
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_event_group = xEventGroupCreate();

    // Register event handlers
    esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);

    // Initialize Wi-Fi in station mode
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Generate a unique service name
    get_device_service_name(service_name, sizeof(service_name));

    // Configure button GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    gpio_config(&io_conf);

    // Create queue and task for button handling
    button_queue = xQueueCreate(5, sizeof(uint32_t));
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, (void *)BUTTON_GPIO);

    // Check if the device is already provisioned
    bool provisioned = false;
    if (wifi_prov_mgr_is_provisioned(&provisioned) != ESP_OK || !provisioned) {
        ESP_LOGI(TAG, "Starting initial provisioning");
        start_provisioning();
    } else {
        ESP_LOGI(TAG, "Already provisioned, connecting...");
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
