#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <inttypes.h>

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
*/
static void send_task(void *pvParameters){

    uint64_t prom;
    while(1){
            xSemaphoreTake(xMutex, portMAX_DELAY);

            if(c){
                prom = x / c;
                x=0;
                c=0;
            }
            else{
                prom = 0;
            }
            xSemaphoreGive(xMutex);

            if(prom){
                if(prom >= 60){
                    ESP_LOGE(TAG, "No hubo sonido en los ultimos 10 segundos. Medida obtenida: %" PRIu64, prom);
                }
                else{
                    ESP_LOGI(TAG, "Se detecto sonido en los ultimos 10 segundos. Medida obtenida: %" PRIu64, prom);
                }
            }
            prom = 0;
        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Pausa de 10s
    }
}


void app_main() {

    gpio_reset_pin(33);
    gpio_set_direction(33, GPIO_MODE_INPUT);
    gpio_set_pull_mode(33, GPIO_PULLUP_ONLY);
    xMutex = xSemaphoreCreateMutex();

    if(xMutex == NULL){
        ESP_LOGE(TAG, "No se creo el mutex");
        return;
    }

    xTaskCreate(sound_task, "mediciones", 8192, NULL, 10, &handle_sound);
    xTaskCreate(send_task, "envio", 8192, NULL, 10, &handle_send);

}
