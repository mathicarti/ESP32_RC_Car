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

typedef struct data_packet
{
    uint8_t servo_angle; // 0 to 180 degrees
    uint8_t esc_speed; // 0 to 100 percent
    uint8_t command; // 0 -> NULL; 1 -> Controller; 2 -> Receiver
} data_packet;

data_packet current_data = {0,0,0};
data_packet rcv_packet;

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
    // make it safe!
    memcpy(&rcv_packet, data, data_len);
    ESP_LOGI(TAG, "Ctrlr Packet: Command: %d, Servo Angle: %d, ESC Speed: %d", rcv_packet.command, rcv_packet.servo_angle, rcv_packet.esc_speed);
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
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_send));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));

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
        esp_err_t result = esp_now_send(mac_addr, (uint8_t *)&current_data, sizeof(current_data));

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
