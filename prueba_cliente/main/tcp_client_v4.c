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
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "stdlib.h"
#include "esp_err.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "freertos/event_groups.h"

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
#define WIFI_CONNECTED_BIT BIT0
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
int flag_r = 1;
char wifi_ssid[33];
char wifi_pass[33];
char wifi_server[33];

uint8_t mac[6];
//int mac;
uint32_t c = 0; //Contador
uint64_t x = 0; //Acumulador
uint64_t prom = 0;
int sock;
char host_ip[] = HOST_IP_ADDR;

//Estructura para almacenar datos de sensor y mediciones a enviar al server
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
    uint8_t mac[6];
}sensor_packet_t;
#pragma pack(pop)

static EventGroupHandle_t wifi_event_group = NULL;

static const char *TAG = "SENSOR";
static const char *TAG_W = "WIFI_AP";
static const char *payload = "Message from ESP32 ";


//AGREGAR SSID y PASSWORD.
//SSID Y PASSWORD se almacenaran en las variables globales del cliente.

//html para el formulario       ES DE PRUEBA
static const char index_html[] = "<!DOCTYPE html>"
    "<html>"
    "<head><title>Configuracion del sensor</title></head>"
    "<body>"
    "<h1>Ingrese la configuracion del sensor</h1>"
    "<form action=\"/config\" method=\"post\">"
    "SSID: <input type=\"text\" name=\"ssid\"/><br/>"
    "Password: <input type=\"text\" name=\"pass\"/><br/>"
    "Servidor: <input type=\"text\" name=\"serv\"/><br/>"
    "ID: <input type=\"text\" name=\"id\"/><br/>"
    "Limite de sonido: <input type=\"text\" name=\"limit_sound\"/><br/>"
    "Piso: <input type=\"text\" name=\"piso\"/><br/>"
    "Tipo: <select name=\"tipo\">"
        "<option value=\"publico\">Publico</option>"
        "<option value=\"privado\">Privado</option>"
    "</select><br/>"
    "Area/Salon: <input type=\"text\" name=\"area\"/><br/>"
    "<input type=\"submit\" value=\"Enviar\"/>"
    "</form>"
    "</body>"
    "</html>";


//handler de eventos de conexion a red
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){
        switch (event_id){
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG_W, "WiFi iniciando, intentando conectar....");
                esp_wifi_connect();
                break;
            
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG_W, "Desconectado. Reintentando...");
                esp_wifi_connect();
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            
            default:
                break;
        }
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_W, "IP asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

//Funcion para configurar wifi en modo sta y conectarse a la red seleccionada (example_connect() pero sin sdkconfig)
esp_err_t wifi_connect(void){
    if(strlen(wifi_ssid) == 0){
        ESP_LOGE(TAG_W, "No se ha configurado la SSID");
        return ESP_FAIL;
    }
    //ESP_LOGI(TAG_W, "xd1");
    
    if(wifi_event_group == NULL){
        wifi_event_group = xEventGroupCreate();
    }
    //ESP_LOGI(TAG_W, "xd2");

    //ESP_ERROR_CHECK(esp_netif_init());
    //ESP_LOGI(TAG_W, "xd3");
    //ESP_ERROR_CHECK(esp_event_loop_create_default());
    //ESP_LOGI(TAG_W, "xd4");
    
    esp_netif_create_default_wifi_sta();
    //ESP_LOGI(TAG_W, "xd5");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    //ESP_LOGI(TAG_W, "xd6");

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, wifi_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, wifi_pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    //ESP_LOGI(TAG_W, "xd7");

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    //ESP_LOGI(TAG_W, "xd8");

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG_W, "Intentando conectar con la red: %s", wifi_ssid);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if(bits & WIFI_CONNECTED_BIT){
        ESP_LOGI(TAG_W, "Conexion exitosa a la red: %s", wifi_ssid);
    }
    else{
        ESP_LOGE(TAG_W, "Error al conectar con la red: %s", wifi_pass);
        return ESP_FAIL;
    }

    return ESP_OK;
}

//Handler para mostrar el formulario
static esp_err_t get_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

//Handler para recibir conf inicial del formulario
static esp_err_t post_config_handler(httpd_req_t *req){
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if(ret <= 0){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error al recibir datos de http");
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI("HTTP_POST", "Datos recibidos: %s", buf);

    char str_id[10], str_limit[10], str_piso[10], str_tipo[20], str_area[30], str_ssid[10], str_pass[10], str_serv[10];
    sscanf(buf, "ssid=%9[^&]&pass=%9[^&]&serv=%9[^&]&id=%9[^&]&limit_sound=%9[^&]&piso=%9[^&]&tipo=%19[^&]&area=%29s", str_ssid, str_pass, str_serv, str_id, str_limit, str_piso, str_tipo, str_area);

    id = atoi(str_id);
    strncpy(wifi_ssid, str_ssid, sizeof(str_ssid) - 1);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    strncpy(wifi_pass, str_pass, sizeof(str_pass) - 1);
    wifi_pass[sizeof(wifi_pass) - 1] = '\0';
    strncpy(wifi_server, str_serv, sizeof(str_serv) - 1);
    wifi_server[sizeof(wifi_server) - 1] = '\0';
    limit_sound = atoi(str_limit);
    piso = atoi(str_piso);
    strncpy(tipo, str_tipo, sizeof(str_tipo) - 1);
    tipo[sizeof(tipo) - 1] = '\0';
    if(strcmp(tipo, "privado") == 0){
        salon_p = atoi(str_area);
        flag_p = 1;
    }
    else{
        strncpy(area, str_area, sizeof(str_area) - 1);
        area[sizeof(area) - 1] = '\0';
        flag_p = 0;
    }
    httpd_resp_send(req, "Configuracion recibida", HTTPD_RESP_USE_STRLEN);
    ESP_LOGI("HTTP_POST", "Configuracion actualizada: wifi_ssid=%s, wifi_pass=%s, wifi_server=%s, id=%d, limit_sound=%d, piso=%d, tipo=%s, area=%s", wifi_ssid, wifi_pass, wifi_server, id, limit_sound, piso, tipo, area);
    flag_r = 0;

    return ESP_OK;
}

//handler para decir que no hay favicon xd
static esp_err_t favicon_handler(httpd_req_t *req){
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

//Formulario
static httpd_uri_t index_uri = {
    .uri        =   "/",
    .method     =   HTTP_GET,
    .handler    =   get_handler,
    .user_ctx   =   NULL
};

//Envio de datos
static httpd_uri_t config_uri = {
    .uri        =   "/config",
    .method     =   HTTP_POST,
    .handler    =   post_config_handler,
    .user_ctx   =   NULL,
};

//Favicon que no existe xd
static httpd_uri_t favicon_uri = {
    .uri        =   "/favicon.ico",
    .method     =   HTTP_GET,
    .handler    =   favicon_handler,
    .user_ctx   =   NULL,
};

//Funcion para iniciar server de configuracion inicial
static httpd_handle_t start_http_server(void){
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    //max_header antes era 512
    if(httpd_start(&server, &config) == ESP_OK){
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &config_uri);
        httpd_register_uri_handler(server, &favicon_uri);
    }

    return server;
}

//Configuracion de conexion wifi modo AP
void wifi_init_softap(void){
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t conf = {
        .ap = {
            .ssid = "SENSOR_CONFIG",
            .ssid_len = 0,
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    if(strlen((char *)conf.ap.password) == 0){
        conf.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &conf));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI("WIFI", "SoftAP iniciando con SSID: %s", conf.ap.ssid);
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
    char rx_buffer[128], rx_buffer2[128];
    sensor_packet_t packet;
    //uint8_t buffer[sizeof(int) + sizeof(int)];
    while(1){

        xSemaphoreTake(xMutex, portMAX_DELAY);
        
        prom = x / c;

        ESP_LOGI(TAG, "Promedio de sonido es: %" PRIu64, prom);

        if((prom < limit_sound) && (prom !=0)){
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
        //packet.mac = mac;
        memcpy(packet.mac, mac, sizeof(packet.mac));

        x = 0;
        c = 0;
        prom = 0;
        sound = 0;

        xSemaphoreGive(xMutex);

        int err = send(sock, &packet, sizeof(packet), 0);
        if (err < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            break;
        }

        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        // Error occurred during receiving
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
            break;
        }
        else{
            rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
            ESP_LOGI(TAG, "%s", rx_buffer);

            if(!strcmp(rx_buffer, "ON_LED")){  //Recibe comando para encender el LED
                gpio_set_level(2, 1);
            }
            else if(!strcmp(rx_buffer, "OFF_LED")){ //Recibe comando para apagar el LED
                gpio_set_level(2,0);
            }

            int len2 = recv(sock, rx_buffer2, sizeof(rx_buffer2) - 1, 0);
            if(len2 < 0){
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            else{
                rx_buffer2[len2] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len2, host_ip);
                ESP_LOGI(TAG, "%s", rx_buffer2);

                if(!strcmp(rx_buffer2, "NACK")){
                    int len3 = recv(sock, &packet, sizeof(sensor_packet_t), 0);
                    if(len3 < 0){
                        ESP_LOGE(TAG, "recv failed: errno %d", errno);
                        break;
                    }
                    else if(len3 != sizeof(sensor_packet_t)){
                        ESP_LOGE(TAG, "recv failed: Tamanio de paquete recibido erroneo ");
                        break;
                    }
                }
            }
        }

        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Pausa de 10s
    }
}

void tcp_client(void)
{
    //Entra a la pagina con http://192.168.4.1/
    wifi_init_softap();
    httpd_handle_t server = start_http_server();
    //int f1 = 1;

    while(flag_r){
        vTaskDelay(500 / portTICK_PERIOD_MS);
        //f1 = 0;
    }

    if(server){
        httpd_stop(server);
    }

    //Variables de prueba para ver si funciona el wifi_connect()
    //wifi_ssid = "ALEX82";
    
    //wifi_pass = "12345678";
    int del = 1;
    while(del <= 200){
        ESP_LOGI(TAG_W, "Tiempo para conectar raspberry: %d / 200", del);
        del++;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    //Todavia faltaria hacer que el usuario pueda ingressar ssid y password mediante el formulario web

    if(wifi_connect() == ESP_OK){
        gpio_reset_pin(33);
        gpio_set_direction(33, GPIO_MODE_INPUT);
        gpio_set_pull_mode(33, GPIO_PULLUP_ONLY);

        gpio_reset_pin(2);
        gpio_set_direction(2, GPIO_MODE_OUTPUT);

        //uart_init();
        //serial_config_init();
        
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

            //esp_read_mac(mac, ESP_MAC_WIFI_STA);
            //esp_base_mac_addr_get()
            esp_err_t xd = esp_wifi_get_mac(WIFI_IF_STA, mac);
            if (xd == ESP_OK) {
                ESP_LOGI(TAG, "Dirección MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            } else {
                ESP_LOGE(TAG, "No se pudo obtener la dirección MAC: %s", esp_err_to_name(err));
            }

            //AGREGAR MAIN DE prueba_mic AQUI
            xTaskCreate(sound_task, "mediciones", 8192, NULL, 10, &handle_sound);
            xTaskCreate(send_task, "envio", 8192, NULL, 10, &handle_send);

            while(1){
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        }
    }
    else{
        ESP_LOGE(TAG_W, "Error de conexion, no se pudo conectar a wifi en modo STA");
    }
}
