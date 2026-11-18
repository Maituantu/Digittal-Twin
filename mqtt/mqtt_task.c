#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <mosquitto.h>
#include <string.h>
#define MQTT_BROKER      "mqtt.mysignage.vn"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "pi01-mqtt"

#define MQTT_SUB_TOPIC   "test/topic"
#define MQTT_PUB_TOPIC   "test/reply"

#define MQTT_USERNAME    NULL
#define MQTT_PASSWORD    NULL


/* Số lần reconnect thành công */
static unsigned long reconnect_count = 0;

/* Đã từng kết nối thành công chưa? */
static int connected_once = 0;


/* =========================================================
 * Callback: MQTT connect
 * ========================================================= */
static void on_connect(struct mosquitto *mosq,
                       void *userdata,
                       int rc)
{
    if (rc == 0) {

        if (connected_once == 0) {
            /* Đây là lần kết nối đầu tiên */
            connected_once = 1;

            printf("[MQTT] Connected to broker\n");
        }
        else {
            /* Đây là một lần reconnect thành công */
            reconnect_count++;

            printf("[MQTT] Reconnected successfully\n");
            printf("[MQTT] Reconnect count: %lu\n",
                   reconnect_count);
        }

        /* Subscribe sau mỗi lần connect/reconnect */
        int sub_rc = mosquitto_subscribe(
            mosq,
            NULL,
            MQTT_SUB_TOPIC,
            0
        );

        if (sub_rc != MOSQ_ERR_SUCCESS) {

            fprintf(stderr,
                    "[MQTT] Subscribe failed: %s\n",
                    mosquitto_strerror(sub_rc));

        } else {

            printf("[MQTT] Subscribed to: %s\n",
                   MQTT_SUB_TOPIC);
        }

    } else {

        printf("[MQTT] Connect failed, rc = %d\n", rc);
    }
}


/* =========================================================
 * Callback: MQTT disconnect
 * ========================================================= */
static void on_disconnect(struct mosquitto *mosq,
                          void *userdata,
                          int rc)
{
    printf("[MQTT] Disconnected, rc = %d\n", rc);
}


/* =========================================================
 * Callback: nhận MQTT message
 * ========================================================= */
static void on_message(struct mosquitto *mosq,
                       void *userdata,
                       const struct mosquitto_message *msg)
{
    if (msg == NULL || msg->payload == NULL) {
        return;
    }

    printf("\n[MQTT] Message received\n");

    printf("[MQTT] Topic   : %s\n",
           msg->topic);

    printf("[MQTT] Payload : %.*s\n",
           msg->payloadlen,
           (char *)msg->payload);


    /* Tạo phản hồi */
    const char *response = "ACK";
    int len = strlen(response);
    


    /* Publish phản hồi */
    int pub_rc = mosquitto_publish(
        mosq,
        NULL,
        MQTT_PUB_TOPIC,
        len,
        response,
        0,
        false
    );

    if (pub_rc != MOSQ_ERR_SUCCESS) {

        fprintf(stderr,
                "[MQTT] Publish failed: %s\n",
                mosquitto_strerror(pub_rc));

    } else {

        printf("[MQTT] Response sent: %s\n",
               response);
    }
}


/* =========================================================
 * MQTT THREAD
 * ========================================================= */
static void *mqtt_thread(void *arg)
{
    struct mosquitto *mosq;
    int rc;

    printf("[MQTT] Thread started\n");


    /* =====================================================
     * 1. Khởi tạo libmosquitto
     * ===================================================== */
    mosquitto_lib_init();


    /* =====================================================
     * 2. Tạo MQTT client
     * ===================================================== */
    mosq = mosquitto_new(
        MQTT_CLIENT_ID,
        true,
        NULL
    );

    if (mosq == NULL) {

        fprintf(stderr,
                "[MQTT] Failed to create client\n");

        mosquitto_lib_cleanup();

        return NULL;
    }


    /* =====================================================
     * 3. Báo cho libmosquitto biết app dùng thread
     * ===================================================== */
    rc = mosquitto_threaded_set(mosq, true);

    if (rc != MOSQ_ERR_SUCCESS) {

        fprintf(stderr,
                "[MQTT] Threaded mode failed: %s\n",
                mosquitto_strerror(rc));

        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();

        return NULL;
    }


    /* =====================================================
     * 4. Cấu hình reconnect delay
     *
     * 2s → 4s → 8s → 10s → 10s → ...
     * ===================================================== */
    rc = mosquitto_reconnect_delay_set(
        mosq,
        2,
        10,
        true
    );

    if (rc != MOSQ_ERR_SUCCESS) {

        fprintf(stderr,
                "[MQTT] Reconnect setup failed: %s\n",
                mosquitto_strerror(rc));

        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();

        return NULL;
    }


    /* =====================================================
     * 5. Đăng ký callback
     * ===================================================== */
    mosquitto_connect_callback_set(
        mosq,
        on_connect
    );

    mosquitto_disconnect_callback_set(
        mosq,
        on_disconnect
    );

    mosquitto_message_callback_set(
        mosq,
        on_message
    );


    /* =====================================================
     * 6. Username/password nếu broker yêu cầu
     * ===================================================== */
    if (MQTT_USERNAME != NULL &&
        MQTT_PASSWORD != NULL) {

        rc = mosquitto_username_pw_set(
            mosq,
            MQTT_USERNAME,
            MQTT_PASSWORD
        );

        if (rc != MOSQ_ERR_SUCCESS) {

            fprintf(stderr,
                    "[MQTT] Authentication setup failed: %s\n",
                    mosquitto_strerror(rc));

            mosquitto_destroy(mosq);
            mosquitto_lib_cleanup();

            return NULL;
        }
    }


    /* =====================================================
     * 7. Kết nối lần đầu
     *
     * Nếu fail → thử lại sau 2 giây
     * ===================================================== */
    while (1) {

        printf("[MQTT] Connecting to %s:%d ...\n",
               MQTT_BROKER,
               MQTT_PORT);

        rc = mosquitto_connect(
            mosq,
            MQTT_BROKER,
            MQTT_PORT,
            60
        );

        if (rc == MOSQ_ERR_SUCCESS) {

            printf("[MQTT] Initial connection accepted\n");

            break;
        }


        fprintf(stderr,
                "[MQTT] Initial connect failed: %s\n",
                mosquitto_strerror(rc));

        printf("[MQTT] Retry in 2 seconds...\n");

        sleep(2);
    }


    /* =====================================================
     * 8. Giữ MQTT thread chạy liên tục
     *
     * Nếu mất mạng:
     *
     * loop_forever()
     *      ↓
     * tự reconnect
     *      ↓
     * on_connect()
     *      ↓
     * subscribe lại
     * ===================================================== */
    printf("[MQTT] MQTT loop started\n");

    rc = mosquitto_loop_forever(
        mosq,
        -1,
        1
    );


    /* =====================================================
     * 9. Nếu loop thực sự dừng
     * ===================================================== */
    if (rc != MOSQ_ERR_SUCCESS) {

        fprintf(stderr,
                "[MQTT] MQTT loop stopped: %s\n",
                mosquitto_strerror(rc));
    }


    /* =====================================================
     * 10. Cleanup
     * ===================================================== */
    mosquitto_destroy(mosq);

    mosquitto_lib_cleanup();

    return NULL;
}


/* =========================================================
 * MAIN
 * ========================================================= */
int main(void)
{
    pthread_t mqtt_tid;

    printf("=== Raspberry Pi Application ===\n");


    /* Tạo MQTT thread */
    if (pthread_create(
            &mqtt_tid,
            NULL,
            mqtt_thread,
            NULL
        ) != 0) {

        perror("Failed to create MQTT thread");

        return EXIT_FAILURE;
    }


    /*
     * Sau này:
     *
     * pthread_create(&camera_tid,
     *                NULL,
     *                camera_thread,
     *                NULL);
     *
     * pthread_create(&uart_tid,
     *                NULL,
     *                uart_thread,
     *                NULL);
     */


    /* Chờ MQTT thread */
    pthread_join(
        mqtt_tid,
        NULL
    );


    return EXIT_SUCCESS;
}
