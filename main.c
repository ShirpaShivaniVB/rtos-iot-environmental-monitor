/*
 * RTOS-Based IoT Environmental Monitoring System
 * Hardware: ESP32
 * RTOS: FreeRTOS
 * Protocol: MQTT over WiFi
 *
 * Tasks:
 *   - Sensor Task: reads temperature and humidity from DHT22
 *   - MQTT Task: publishes sensor data to broker
 *   - LED Task: blinks status LED
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "dht.h"
#include "config.h"

static const char *TAG = "ENV_MONITOR";

/* Queue to pass sensor data from sensor task to MQTT task */
static QueueHandle_t sensor_data_queue;

/* Semaphore to signal WiFi connection */
static SemaphoreHandle_t wifi_connected_sem;

/* MQTT client handle */
static esp_mqtt_client_handle_t mqtt_client;

/* Sensor data structure */
typedef struct {
    float temperature;
    float humidity;
    int64_t timestamp_ms;
} sensor_data_t;

/* ─────────────────────────────────────────────
 *  WiFi Event Handler
 * ───────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected, IP obtained");
        xSemaphoreGive(wifi_connected_sem);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    }
}

/* ─────────────────────────────────────────────
 *  WiFi Initialization
 * ───────────────────────────────────────────── */
static void wifi_init(void)
{
    wifi_connected_sem = xSemaphoreCreateBinary();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Wait until connected */
    xSemaphoreTake(wifi_connected_sem, portMAX_DELAY);
}

/* ─────────────────────────────────────────────
 *  MQTT Event Handler
 * ───────────────────────────────────────────── */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            break;
        default:
            break;
    }
}

/* ─────────────────────────────────────────────
 *  MQTT Initialization
 * ───────────────────────────────────────────── */
static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* ─────────────────────────────────────────────
 *  Task 1: Sensor Acquisition Task
 *  Reads DHT22 every SENSOR_READ_INTERVAL_MS
 *  Pushes data to queue (non-blocking)
 * ───────────────────────────────────────────── */
static void sensor_task(void *pvParameters)
{
    sensor_data_t data;
    ESP_LOGI(TAG, "Sensor task started");

    while (1) {
        esp_err_t result = dht_read_float_data(DHT_TYPE_DHT22, DHT_GPIO_PIN,
                                               &data.humidity, &data.temperature);
        if (result == ESP_OK) {
            data.timestamp_ms = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "Temp: %.1f°C  Humidity: %.1f%%", data.temperature, data.humidity);

            /* Send to queue; drop reading if queue is full (non-blocking) */
            if (xQueueSend(sensor_data_queue, &data, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Queue full, dropping sensor reading");
            }
        } else {
            ESP_LOGE(TAG, "Failed to read DHT22 sensor");
        }

        /* Use RTOS delay — never busy-wait */
        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

/* ─────────────────────────────────────────────
 *  Task 2: MQTT Publish Task
 *  Waits for data from queue, publishes to broker
 * ───────────────────────────────────────────── */
static void mqtt_publish_task(void *pvParameters)
{
    sensor_data_t data;
    char payload[128];
    ESP_LOGI(TAG, "MQTT publish task started");

    while (1) {
        /* Block until sensor data is available */
        if (xQueueReceive(sensor_data_queue, &data, portMAX_DELAY) == pdTRUE) {
            snprintf(payload, sizeof(payload),
                     "{\"temperature\":%.1f,\"humidity\":%.1f,\"timestamp\":%lld}",
                     data.temperature, data.humidity, data.timestamp_ms);

            int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, payload, 0, 1, 0);
            if (msg_id >= 0) {
                ESP_LOGI(TAG, "Published: %s", payload);
            } else {
                ESP_LOGE(TAG, "MQTT publish failed");
            }
        }
    }
}

/* ─────────────────────────────────────────────
 *  Task 3: LED Status Task
 *  Blinks LED to indicate system is running
 * ───────────────────────────────────────────── */
static void led_task(void *pvParameters)
{
    gpio_set_direction(STATUS_LED_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(STATUS_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(STATUS_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1900));
    }
}

/* ─────────────────────────────────────────────
 *  app_main
 * ───────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== RTOS IoT Environmental Monitor ===");

    /* Initialize NVS (required for WiFi) */
    ESP_ERROR_CHECK(nvs_flash_init());

    /* Create queue for sensor data (holds up to 5 readings) */
    sensor_data_queue = xQueueCreate(5, sizeof(sensor_data_t));
    configASSERT(sensor_data_queue != NULL);

    /* Connect to WiFi, then start MQTT */
    wifi_init();
    mqtt_init();

    /* Create RTOS tasks with defined stack sizes and priorities */
    xTaskCreate(sensor_task,       "sensor_task",  4096, NULL, 5, NULL);
    xTaskCreate(mqtt_publish_task, "mqtt_task",    4096, NULL, 4, NULL);
    xTaskCreate(led_task,          "led_task",     2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "All tasks created. System running.");
}
