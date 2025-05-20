/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "sdkconfig.h"
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>            // struct addrinfo
#include <arpa/inet.h>
#include "esp_netif.h"
#include "esp_log.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdint.h>
#include "esp_mac.h"
#include <sys/param.h>
#include <math.h>

#if defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
#include "addr_from_stdin.h"
#endif

#if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
#define HOST_IP_ADDR ""
#endif

#define PORT CONFIG_EXAMPLE_PORT
#define LIMIT_SOUND 60

TaskHandle_t handle_sound, handle_send;
SemaphoreHandle_t xMutex;

uint32_t c = 0; //Contador
uint64_t x = 0; //Acumulador
uint64_t prom = 0;
int sock;
char host_ip[] = HOST_IP_ADDR;

static const char *TAG = "SENSOR";
static const char *payload = "Message from ESP32 ";

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

//Tarea encargada de recibir comandos del servidor
static void send_task(void *pvParameters){

    //uint64_t prom;
    int id = 5;
    int sound = 0;
    char rx_buffer[128];
    uint8_t buffer[sizeof(int) + sizeof(int)];
    while(1){

        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        // Error occurred during receiving
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
        }
        // Data received
        else {
            rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
            ESP_LOGI(TAG, "%s", rx_buffer);

            if(!strcmp(rx_buffer, "DET_SOUND")){    //Recibe comando para enviar mediciones de sonido al server
                xSemaphoreTake(xMutex, portMAX_DELAY);

                //ESP_LOGI(TAG, "Promedio de sonido es: %" PRIu64, x);
                //ESP_LOGI(TAG, "Promedio de sonido es: %" PRIu32, c);
                prom = x / c;

                ESP_LOGI(TAG, "Promedio de sonido es: %" PRIu64, prom);

                if(prom < LIMIT_SOUND){
                    sound = 1;
                }
                else
                    sound = 0;

                memcpy(buffer, &sound, sizeof(sound));
                memcpy(buffer + sizeof(sound), &id, sizeof(id));
                //memcpy(buffer + sizeof(x) + sizeof(c), &id, sizeof(id));
        
                int err = send(sock, buffer, sizeof(buffer), 0);
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }

                x = 0;
                c = 0;
                prom = 0;
                sound = 0;
        
                xSemaphoreGive(xMutex);
            }
            else if(!strcmp(rx_buffer, "ON_LED")){  //Recibe comando para encender el LED
                gpio_set_level(2, 1);
            }
            else if(!strcmp(rx_buffer, "OFF_LED")){ //Recibe comando para apagar el LED
                gpio_set_level(2,0);
            }
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);  // Pausa de 500ms
    }
}

void tcp_client(void)
{
    gpio_reset_pin(33);
    gpio_set_direction(33, GPIO_MODE_INPUT);
    gpio_set_pull_mode(33, GPIO_PULLUP_ONLY);

    gpio_reset_pin(2);
    gpio_set_direction(2, GPIO_MODE_OUTPUT);
    
    char rx_buffer[128];
    int addr_family = 0;
    int ip_protocol = 0;

    xMutex = xSemaphoreCreateMutex();

    if(xMutex == NULL){
        ESP_LOGE(TAG, "No se creo el mutex");
        return;
    }

    while (1) {
#if defined(CONFIG_EXAMPLE_IPV4)
        struct sockaddr_in dest_addr;
        inet_pton(AF_INET, host_ip, &dest_addr.sin_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
        struct sockaddr_storage dest_addr = { 0 };
        ESP_ERROR_CHECK(get_addr_from_stdin(PORT, SOCK_STREAM, &ip_protocol, &addr_family, &dest_addr));
#endif

        sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Successfully connected");

        //AGREGAR MAIN DE prueba_mic AQUI
        xTaskCreate(sound_task, "mediciones", 8192, NULL, 10, &handle_sound);
        xTaskCreate(send_task, "envio", 8192, NULL, 10, &handle_send);

        while(1){
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
}
