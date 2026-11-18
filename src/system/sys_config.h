#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

// Maximum number of commands waiting for processing (1..65535).
#define COMMAND_QUEUE_CAPACITY 32

// UART Linux device: change to /dev/ttyUSB0 or /dev/ttyACM0 for USB adapters.
#define UART_ENABLED 1
#define UART_DEVICE "/dev/ttyUSB0"
#define UART_BAUD 115200
#define UART_RX_TIMEOUT_MS 200
#define UART_TX_TIMEOUT_MS 1000

// MQTT connection settings. nullptr disables authentication.
#define MQTT_HOST "mqtt.mysignage.vn"
#define MQTT_PORT 1883
#define MQTT_KIT_CODE "KIT01"
#define MQTT_CLIENT_ID "pi-kit01"
#define MQTT_USERNAME nullptr
#define MQTT_PASSWORD nullptr

// Actual UART measurements published as JSON.
#define MQTT_TOPIC_TELEMETRY "lab/kit/" MQTT_KIT_CODE "/telemetry"
#define MQTT_TOPIC_HEARTBEAT "lab/kit/" MQTT_KIT_CODE "/heartbeat"

// MQTT command topics for this kit.
#define MQTT_TOPIC_CMD_RESET "lab/kit/" MQTT_KIT_CODE "/cmd/reset"
#define MQTT_TOPIC_CMD_POWER "lab/kit/" MQTT_KIT_CODE "/cmd/power"
#define MQTT_TOPIC_CMD_VOLTAGE "lab/kit/" MQTT_KIT_CODE "/cmd/voltage"
#define MQTT_TOPIC_CMD_SCALE "lab/kit/" MQTT_KIT_CODE "/cmd/scale"
#define MQTT_TOPIC_CMD_RULER "lab/kit/" MQTT_KIT_CODE "/cmd/ruler"
#define MQTT_TOPIC_CMD_SHUTDOWN "lab/kit/" MQTT_KIT_CODE "/cmd/shutdown"

#endif
