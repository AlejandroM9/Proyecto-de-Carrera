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
#include "driver/uart.h"

#if defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
#include "addr_from_stdin.h"
#endif

#if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
#define HOST_IP_ADDR ""
#endif

#define PORT CONFIG_EXAMPLE_PORT
#define UART0 UART_NUM_0
//#define LIMIT_SOUND 60

TaskHandle_t handle_sound, handle_send, handle_info;
SemaphoreHandle_t xMutex;

int id;
int salon_p;
int limit_sound = 60;
int piso = 1;
char tipo[30] = "Publico";
char area[30] = "Estudio";
int flag_p = 0;

uint32_t c = 0; //Contador
uint64_t x = 0; //Acumulador
uint64_t prom = 0;
int sock;
char host_ip[] = HOST_IP_ADDR;

#pragma pack(push, 1)
typedef struct {
    int sound;
    int id;
    int piso;
    int limit_sound;
    int salon_p;
    char tipo[30];
    char area[30];
    int flag_p;
}sensor_packet_t;
#pragma pack(pop)


static const char *TAG = "SENSOR";
static const char *payload = "Message from ESP32 ";

//Funcion para inicializar uart
static void uart_init(void){
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
    };
    ESP_ERROR_CHECK(uart_param_config(UART0, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART0, 1024*2, 1024*2, 0, NULL, 0));
}

//Funcion para leer teclado por uart
static int uart_gets(char *buf, int len){
    int i = 0;
    char ch;
    while(i < (len - 1)){
        int l = uart_read_bytes(UART0, (uint8_t *)&ch, 1, 20 / portTICK_PERIOD_MS);
        if(l > 0){
            if(ch == '\n' || ch == '\r'){
                if(i == 0)
                    continue;
                
                break;
            }
            buf[i++] = ch;
        }
    }
    buf[i] = '\0';
    return i;
}

//Funcion para la configuracion inicial del esp32/sensor
void serial_config_init(void){
    char buf[100];
    const char *welcome = "\r\n***** BIENVENIDO A LA CONFIGURACION INICIAL DEL SENSOR *****\r\n";
    const char *input_id = "Ingrese el numero id del sensor: ";
    const char *input_limit = "Ingrese el limite de deteccion de sonido del sensor (1-99) : ";
    const char *input_piso = "Ingrese el piso donde se encuentra el sensor (1, 2, 3 o 4): ";
    const char *input_tipo = "Ingrese el tipo del sensor (publico/privado): ";
    const char *input_area = "Ingrese el area de la biblioteca donde se encuentra el sensor: ";
    const char *input_salon = "Ingrese el salon privado donde se encuentra el sensor: ";
    
    uart_write_bytes(UART0, welcome, strlen(welcome));
    
    //Solicita id
    uart_write_bytes(UART0, input_id, strlen(input_id));
    uart_gets(buf, sizeof(buf));
    id = atoi(buf);

    //Solicita threshold
    uart_write_bytes(UART0, input_limit, strlen(input_limit));
    uart_gets(buf, sizeof(buf));
    limit_sound = atoi(buf);

    //Solicita piso
    uart_write_bytes(UART0, input_piso, strlen(input_piso));
    uart_gets(buf, sizeof(buf));
    piso = atoi(buf);

    //Solicita tipo
    uart_write_bytes(UART0, input_tipo, strlen(input_tipo));
    uart_gets(buf, sizeof(buf));
    strncpy(tipo, buf, sizeof(tipo) - 1);
    tipo[sizeof(tipo) - 1] = '\0';
    //char t[30];
    char msg[256];
    
    if(strcmp(tipo, "privado") == 0){
        flag_p = 1;
        //Solicita salon
        uart_write_bytes(UART0, input_salon, strlen(input_salon));
        uart_gets(buf, sizeof(buf));
        salon_p = atoi(buf);

        snprintf(msg, sizeof(msg), "\r\nConfiguracion completada: id=%d, threshold=%d, piso=%d, tipo=%s, salon=%d\r\n", id, limit_sound, piso, tipo, salon_p);
        uart_write_bytes(UART0, msg, strlen(msg));
    }
    else{
        flag_p = 0;
        //Solicita area
        uart_write_bytes(UART0, input_area, strlen(input_area));
        uart_gets(buf, sizeof(buf));
        strncpy(area, buf, sizeof(area) - 1);
        area[sizeof(area) - 1] = '\0';

        snprintf(msg, sizeof(msg), "\r\nConfiguracion completada: id=%d, threshold=%d, piso=%d, tipo=%s, area=%s\r\n", id, limit_sound, piso, tipo, area);
        uart_write_bytes(UART0, msg, strlen(msg));
    }
}

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
    int sound = 0;
    char rx_buffer[128];
    sensor_packet_t packet;
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

                if(prom < limit_sound){
                    sound = 1;
                }
                else
                    sound = 0;

                packet.id = id;
                packet.limit_sound = limit_sound;
                packet.sound = sound;
                packet.piso = piso;
                strncpy(packet.tipo, tipo, sizeof(packet.tipo));
                strncpy(packet.area, area, sizeof(packet.area));
                packet.salon_p = salon_p;
                packet.flag_p = flag_p;
        
                int err = send(sock, &packet, sizeof(packet), 0);
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

    uart_init();
    serial_config_init();
    
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
