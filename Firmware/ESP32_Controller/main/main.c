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

static void blink_LED(const gpio_num_t GPIO_NUM, const int count);

static const char *TAG = "ESP_OUT";

static uint8_t peer_mac[ESP_NOW_ETH_ALEN] = {0xD8, 0xBC, 0x38, 0xFC, 0xB1, 0x78};

void on_data_send(const esp_now_send_info_t *rcv_info, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "Delivery Status: %s", rcv_info->tx_status == WIFI_SEND_SUCCESS ? "Success" : "Fail");
}

void on_data_recv(const esp_now_recv_info_t *src_info, const uint8_t *data, int len)
{
    const uint8_t *src_mac = src_info->src_addr;
    ESP_LOGI(TAG, "Received %d bytes, from MAC %02X:%02X:%02X:%02X:%02X:%02X", 
        len, src_mac[0], src_mac[1], src_mac[2], src_mac[3], src_mac[4], src_mac[5]);
}

static void init_wifi(void)
{
    // init netif for the wifi driver to save callibration data
    ESP_ERROR_CHECK(esp_netif_init());
    // Create event loop for the asynchronous WiFi
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Init WiFi
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

static void init_esp_now(void)
{
    ESP_ERROR_CHECK(esp_now_init());

    // Register send/receive status callbacks
    esp_now_send_cb_t(on_data_send);
    esp_now_recv_cb_t(on_data_recv);

    // add peers
    esp_now_peer_info_t peer = {
        .channel = 0,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, peer_mac, ESP_NOW_ETH_ALEN);

    esp_now_add_peer(&peer);
}

void app_main(void)
{
    // Init NVS for wifi driver and esp
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_flash_init();
    }

    gpio_set_direction(GPIO_NUM_12, GPIO_MODE_OUTPUT);

    init_wifi();
    init_esp_now();

    while (1)
    {
        char *data = "23";

        esp_err_t result = esp_now_send(peer_mac, (uint8_t *)&data, sizeof(data));

        if (result == ESP_OK)
        {
            ESP_LOGI(TAG, "Data sent awating callback");
            blink_LED(GPIO_NUM_12, 2);
        }

        else if (result == ESP_ERR_ESPNOW_ARG)
        {
            ESP_LOGE(TAG, "Invalide arguments passed");
        }

        else if (result == ESP_ERR_ESPNOW_NOT_INIT)
        {
            ESP_LOGE(TAG, "ESP NOW not initialised properly");
        }

        else if (result == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Couldn't find peer");
        }

        else if (result == ESP_ERR_ESPNOW_IF)
        {
            ESP_LOGE(TAG, "WiFi interface mismatch");
        }

        else if (result == ESP_ERR_ESPNOW_CHAN)
        {
            ESP_LOGE(TAG, "Peer on wrong channel");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

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