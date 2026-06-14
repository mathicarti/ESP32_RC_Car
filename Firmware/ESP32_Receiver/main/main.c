#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "ESP_OUT";

uint8_t mac_addr[ESP_NOW_ETH_ALEN] = {0xe4, 0x65, 0xb8, 0x75, 0xbb, 0x2c};

static void blink_LED(const gpio_num_t GPIO_NUM, const int count)
{
    for (int i = 0; i < count; i++)
    {
        gpio_set_level(GPIO_NUM, 1);
        vTaskDelay(50);
        gpio_set_level(GPIO_NUM, 0);
        vTaskDelay(50);
    }
}

void on_data_recv(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int data_len)
{
    blink_LED(GPIO_NUM_2, 2);
    const uint8_t *src_mac = esp_now_info->src_addr;
    ESP_LOGI(TAG, "Received %d bytes, from MAC %02X:%02X:%02X:%02X:%02X:%02X", 
        data_len, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
    ESP_LOGI(TAG, "Recived data: %i", *data);
}

void on_data_send(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "Delivery Status: %s", tx_info->tx_status == WIFI_SEND_SUCCESS ? "Success" : "Fail");
}

void init_wifi(void)
{
    // init netif for wifi module
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // init wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // start wifi
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

void init_esp_now(void)
{
    // init esp_now
    ESP_ERROR_CHECK(esp_now_init());

    // Set up the callbacks
    esp_now_recv_cb_t(on_data_recv);
    esp_now_send_cb_t(on_data_send);

    // add peers
    esp_now_peer_info_t peer = {
        .channel = 1,
        .ifidx = WIFI_IF_STA,
        .encrypt = false
    };
    memcpy(peer.peer_addr, mac_addr, ESP_NOW_ETH_ALEN);
    esp_now_add_peer(&peer);
}

void app_main(void)
{
    // init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);

    // init wifi
    init_wifi();

    // init esp_now
    init_esp_now();

    // Run the main processes to wait for callback
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
