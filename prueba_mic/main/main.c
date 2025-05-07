#include "sdkconfig.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "esp_mac.h"
#include <sys/param.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
//#include "protocol_examples_common.h"
#include <math.h>
#include <errno.h>
#include <netdb.h>            // struct addrinfo

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

TaskHandle_t handle_sound, handle_send;
SemaphoreHandle_t xMutex;

uint32_t c = 0; //Contador
uint64_t x = 0; //Acumulador

static const char *TAG = "SENSOR";

//Tarea encargada de almacenar las mediciones del sensor
static void sound_task(void *pvParameters){

    while (1) {

        xSemaphoreTake(xMutex, portMAX_DELAY);
        for(int cont = 0; cont < 100; cont++){
            x += gpio_get_level(33);    //Sumatoria de meciciones
        }  //Cada 100 medidas del acumulador...

        c++;    //...se aumenta el contador, asegurandose que las medidas sean multiplos de 100
        xSemaphoreGive(xMutex);

        vTaskDelay(10 / portTICK_PERIOD_MS);  // Pausa de 10ms
    }
}


/* 
Prueba de detección de sonido, esta tarea toma el acumulador y contador
para calcular un promedio y decidir si se detectó un sonido intenso.

Se tiene planeado que en la versión final este proceso sea realizado en el servidor, 
por lo que esta tarea se encargará de recibir comandos del servidor. 

    COMANDOS PLANEADOS: 
        - DET_SOUND = Enviará el acumulador, contador y numero de id al server (Esta sería la única que interfiere con sound_task). 
        - ON_LED    = Encender led o luz conectada al cliente. 
        - OFF_DEL   = Apagar led o luz conectada al cliente. 

Hace falta tambien hacer una conexion tcp con el server

*/
static void send_task(void *pvParameters){

    //uint64_t prom;
    int id = 5;
    uint8_t buffer[sizeof(uint64_t) + sizeof(uint32_t) + sizeof(int)];
    while(1){
            xSemaphoreTake(xMutex, portMAX_DELAY);

            memcpy(buffer, &x, sizeof(x));
            memcpy(buffer + sizeof(x), &c, sizeof(c));
            memcpy(buffer + sizeof(x) + sizeof(c), &id, sizeof(id));

            //int err = send(sock, buffer, sizeof(buffer), 0);

            xSemaphoreGive(xMutex);
        vTaskDelay(500 / portTICK_PERIOD_MS);  // Pausa de 10s
    }
}


void app_main() {

    gpio_reset_pin(33);
    gpio_set_direction(33, GPIO_MODE_INPUT);
    gpio_set_pull_mode(33, GPIO_PULLUP_ONLY);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    xMutex = xSemaphoreCreateMutex();

    if(xMutex == NULL){
        ESP_LOGE(TAG, "No se creo el mutex");
        return;
    }

    xTaskCreate(sound_task, "mediciones", 8192, NULL, 10, &handle_sound);
    xTaskCreate(send_task, "envio", 8192, NULL, 10, &handle_send);

}
