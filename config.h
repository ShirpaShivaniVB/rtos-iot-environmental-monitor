#pragma once

/* ── WiFi Configuration ─────────────────────── */
#define WIFI_SSID         "your_wifi_ssid"
#define WIFI_PASSWORD     "your_wifi_password"

/* ── MQTT Broker Configuration ──────────────── */
#define MQTT_BROKER_URI   "mqtt://broker.hivemq.com"
#define MQTT_USERNAME     ""
#define MQTT_PASSWORD     ""
#define MQTT_TOPIC        "env_monitor/data"

/* ── Hardware Pin Configuration ─────────────── */
#define DHT_GPIO_PIN      GPIO_NUM_4
#define STATUS_LED_GPIO   GPIO_NUM_2

/* ── Timing Configuration ───────────────────── */
#define SENSOR_READ_INTERVAL_MS   5000   /* Read sensor every 5 seconds */
