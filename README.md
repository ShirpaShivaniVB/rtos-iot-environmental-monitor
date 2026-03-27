# RTOS-Based IoT Environmental Monitoring System

A real-time environmental monitoring system built on **ESP32** using **FreeRTOS**, **Embedded C**, and **MQTT**. The system reads temperature and humidity data from a DHT22 sensor and publishes it to an MQTT broker over WiFi — all using a clean multitask RTOS architecture with no blocking delays.

---

## Features

- Multitask firmware using FreeRTOS (sensor task, MQTT publish task, LED status task)
- Non-blocking sensor reads using RTOS queues between tasks
- MQTT JSON payload published to cloud broker in real time
- Status LED blinks using dedicated RTOS task — no polling
- Automatic WiFi reconnection on disconnection

---

## Hardware Required

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 DevKit V1 |
| Sensor | DHT22 (temperature + humidity) |
| LED | Any 3.3V LED + 330Ω resistor |

### Wiring

```
DHT22 DATA  →  GPIO4
LED+        →  GPIO2 (onboard LED)
DHT22 VCC   →  3.3V
DHT22 GND   →  GND
```

---

## Software Requirements

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [esp-idf-lib](https://github.com/UncleRus/esp-idf-lib) (DHT22 driver)

---

## Setup & Flash

```bash
# Clone the repo
git clone https://github.com/ShirpaShivaniVB/rtos-iot-environmental-monitor
cd rtos-iot-environmental-monitor

# Edit your credentials
nano main/config.h   # Set WIFI_SSID, WIFI_PASSWORD, MQTT_BROKER_URI

# Build and flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## MQTT Output

Data is published to topic `env_monitor/data` as JSON:

```json
{
  "temperature": 28.5,
  "humidity": 62.3,
  "timestamp": 1234567890
}
```

You can subscribe using any MQTT client:
```bash
mosquitto_sub -h broker.hivemq.com -t "env_monitor/data"
```

---

## Architecture

```
app_main()
    ├── wifi_init()         → connects to WiFi
    ├── mqtt_init()         → connects to MQTT broker
    │
    ├── sensor_task         → reads DHT22 every 5s → pushes to queue
    ├── mqtt_publish_task   → receives from queue → publishes JSON
    └── led_task            → blinks status LED every 2s
```

Tasks communicate via a FreeRTOS queue — no shared globals, no blocking delays.

---

## Skills Demonstrated

`Embedded C` `FreeRTOS` `ESP32` `MQTT` `WiFi` `GPIO` `RTOS task design` `Inter-task communication`
