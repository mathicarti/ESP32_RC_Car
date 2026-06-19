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

typedef struct data_packet
{
    uint8_t servo_angle; // 0 to 180 degrees
    uint8_t esc_speed; // 0 to 100 percent
    uint8_t command; // 0 -> NULL; 1 -> Controller; 2 -> Receiver
} data_packet;

data_packet rcv_packet = {1,1,1};

static uint8_t peer_mac[ESP_NOW_ETH_ALEN] = {0xd8, 0xbc, 0x38, 0xfc, 0xb1, 0x78};

void on_data_send(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    bool tx_status = tx_info->tx_status == WIFI_SEND_SUCCESS ? true : false;
    if (tx_status) blink_LED(GPIO_NUM_12, 2);
    ESP_LOGI(TAG, "Delivery Status: %s", tx_status ? "Success" : "Fail");
}

void on_data_recv(const esp_now_recv_info_t * esp_now_info, const uint8_t *data, int data_len)
{
    memcpy(&rcv_packet, data, data_len);
    ESP_LOGI(TAG, "Rcv Packet: Command: %d, Servo Angle: %d, ESC Speed: %d", rcv_packet.command, rcv_packet.servo_angle, rcv_packet.esc_speed);
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
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_send));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

    // add peers
    esp_now_peer_info_t peer = {
        .channel = 1,
        .ifidx = WIFI_IF_STA,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, peer_mac, ESP_NOW_ETH_ALEN);

    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
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
        data_packet send = {
            .servo_angle = 80,
            .esc_speed = 10,
            .command = 1
        };

        esp_err_t result = esp_now_send(peer_mac, (uint8_t *)&send, sizeof(send));

        vTaskDelay(pdMS_TO_TICKS(500));
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