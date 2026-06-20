#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "driver/ledc.h"

#define SERVO_GPIO GPIO_NUM_12

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

QueueHandle_t command_queue_handle = NULL;

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
    memcpy(&rcv_packet, data, data_len);
    xQueueSend(command_queue_handle, &rcv_packet, 0);
}

void on_data_send(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    // ESP_LOGI(TAG, "Delivery Status: %s", tx_info->tx_status == WIFI_SEND_SUCCESS ? "Success" : "Fail");
    ;
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

uint32_t angle_to_duty(uint8_t angle)
{
    if (angle > 180) angle = 180;
    // Maps angle (0deg -> 0.5ms, 180deg -> 2.5ms)
    float pulse_width = 0.5 + (angle / 180.0) * 2.0;
    // convert pulse width to duty cycle (12bit res. 50Hz)
    uint32_t duty = (pulse_width / 20.0) * 4096;
    return duty;
}

void servo_PWM_task(void *pvParameters)
{
    // init PWM for servo (id 0) and esc (id 1)
    ledc_timer_config_t led_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, // Use Low-Speed mode (better stability)
        .timer_num = LEDC_TIMER_0, // Timer 0 for PWM
        .duty_resolution = LEDC_TIMER_12_BIT, // 12-bit resolution -> 4096 levels
        .freq_hz = 50, // 50 Hz for SG90
        .clk_cfg = LEDC_AUTO_CLK // Auto select clock source
    };
    ledc_timer_config(&led_timer);

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER_0
    };
    ledc_channel_config(&ledc_channel);

    while (1)
    {
        // Retrieve what command is in the queue (will be changed for a mutex) and copy it to its own buffer 
        // to servo pos.
        if (xQueueReceive(command_queue_handle, &current_data, portMAX_DELAY) == pdPASS)
        {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, angle_to_duty(current_data.servo_angle));
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(10);
        }
    }
}

void esp_now_data_task(void *pvParameters)
{
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

    // Start Queue to store commands up to 10 structs
    command_queue_handle = xQueueCreate(10, sizeof(data_packet));
    if (command_queue_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to init queue");
        return;
    }

    // need to make some freeRTOS tasks as this is getting tricky
    xTaskCreate(esp_now_data_task, "ESP NOW", 2048, NULL, 3, NULL);
    xTaskCreate(servo_PWM_task, "Servo PWM", 2048, NULL, 5, NULL);
}
