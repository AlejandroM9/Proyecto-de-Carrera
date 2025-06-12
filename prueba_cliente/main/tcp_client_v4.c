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
#include "nvs_flash.h"
#include "nvs.h"
#include <stdbool.h>

#if defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
#include "addr_from_stdin.h"
#endif

#if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
#define HOST_IP_ADDR ""
#endif

//#define PORT CONFIG_EXAMPLE_PORT
#define UART0 UART_NUM_0
#define WIFI_CONNECTED_BIT BIT0
//#define LIMIT_SOUND 60

#define NVS_NAMESPACE "storage"

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
int wifi_port;

uint8_t mac[6];
//int mac;
uint32_t c = 0; //Contador
uint64_t x = 0; //Acumulador
uint64_t prom = 0;
int sock;
char host_ip[33]; //192.168.137.218

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

//html para el formulario       ES DE PRUEBA
static const char index_html[] = 
"<!DOCTYPE html>\n"
"<html lang=\"es\">\n"
"<head>\n"
"  <meta charset=\"UTF-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"  <title>Configuración del sensor</title>\n"
"\n"
"  <style>\n"
"  body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#f4f6f9;margin:0;padding:0}\n"
"  .container{max-width:480px;margin:auto;padding:24px}\n"
"  .card{background:#fff;border-radius:8px;box-shadow:0 0.5rem 1rem rgba(0,0,0,.1);padding:24px;margin-bottom:24px}\n"
"  h1{font-size:1.5rem;margin-top:0;text-align:center}\n"
"  label{display:block;font-weight:600;margin-bottom:4px}\n"
"  input,select{width:100%;padding:8px 10px;margin-bottom:12px;border:1px solid #ccc;border-radius:4px;font-size:1rem}\n"
"  button{display:block;width:100%;padding:10px 0;background:#198754;color:#fff;border:none;border-radius:4px;font-size:1rem;cursor:pointer}\n"
"  button:active{transform:scale(.98)}\n"
"  .alert{padding:10px 14px;margin-bottom:12px;border-radius:4px}\n"
"  .alert.ok{background:#d1e7dd;color:#0f5132}\n"
"  .alert.err{background:#f8d7da;color:#842029}\n"
"  </style>\n"
"\n"
"  <script>\n"
"  function validar(e){\n"
"    e.preventDefault();\n"
"    const f=e.target;\n"
"    for(const el of f.querySelectorAll('[required]')){\n"
"      if(!el.value.trim()){mensaje('Rellena todos los campos',false);return;}\n"
"    }\n"
"    const body=`ssid=${encodeURIComponent(f.ssid.value)}&`\n"
"              +`pass=${encodeURIComponent(f.pass.value)}&`\n"
"              +`serv=${encodeURIComponent(f.serv.value)}&`\n"
"              +`port=${encodeURIComponent(f.port.value)}&`\n"
"              +`id=${encodeURIComponent(f.id.value)}&`\n"
"              +`limit_sound=${encodeURIComponent(f.limit_sound.value)}&`\n"
"              +`piso=${encodeURIComponent(f.piso.value)}&`\n"
"              +`tipo=${encodeURIComponent(f.tipo.value)}&`\n"
"              +`area=${encodeURIComponent(f.area.value)}`;\n"
"    fetch('/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
"      .then(r=>r.text())\n"
"      .then(t=>{mensaje(t,true);setTimeout(()=>location.reload(),800);})\n"
"      .catch(()=>mensaje('Error al enviar',false));\n"
"  }\n"
"  function mensaje(txt,ok){\n"
"    const d=document.getElementById('msg');\n"
"    d.textContent=txt;\n"
"    d.className='alert '+(ok?'ok':'err');\n"
"  }\n"
"  </script>\n"
"</head>\n"
"<body>\n"
"<div class=\"container\">\n"
"  <div class=\"card\">\n"
"    <h1>Configuración del sensor</h1>\n"
"    <form onsubmit=\"validar(event)\">\n"
"      <label>SSID Wi-Fi</label>\n"
"      <input name=\"ssid\" required>\n"
"\n"
"      <label>Password Wi-Fi</label>\n"
"      <input name=\"pass\" type=\"password\" required>\n"
"\n"
"      <label>IP / Host del servidor</label>\n"
"      <input name=\"serv\" required>\n"
"\n"
"      <label>Puerto</label>\n"
"      <input name=\"port\" type=\"number\" min=\"1\" max=\"65535\" required>\n"
"\n"
"      <label>ID del sensor</label>\n"
"      <input name=\"id\" type=\"number\" min=\"0\" required>\n"
"\n"
"      <label>Límite de sonido</label>\n"
"      <input name=\"limit_sound\" type=\"number\" min=\"0\" required>\n"
"\n"
"      <label>Piso</label>\n"
"      <input name=\"piso\" type=\"number\" min=\"0\" required>\n"
"\n"
"      <label>Tipo</label>\n"
"      <select name=\"tipo\" required>\n"
"        <option value=\"publico\">Público</option>\n"
"        <option value=\"privado\">Privado</option>\n"
"      </select>\n"
"\n"
"      <label>Área / Salón</label>\n"
"      <input name=\"area\" required>\n"
"\n"
"      <button>Guardar y reiniciar Wi-Fi</button>\n"
"    </form>\n"
"    <div id=\"msg\"></div>\n"
"  </div>\n"
"</div>\n"
"</body>\n"
"</html>\n";


//Funcion para almacenar configuracion inicial en la memoria flash
void save_config(void){
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "Error abriendo NVS (%s)", esp_err_to_name(err));
        return;
    }

    err = nvs_set_i32(handle, "id", id);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando id");
    err = nvs_set_i32(handle, "piso", piso);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando piso");
    err = nvs_set_i32(handle, "limit", limit_sound);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando limit_sound");
    err = nvs_set_i32(handle, "salon", salon_p);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando salon_p");
    err = nvs_set_str(handle, "tipo", tipo);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando area");
    err = nvs_set_str(handle, "area", area);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando area");
    err = nvs_set_i32(handle, "flag", flag_p);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando flag_p");
    err = nvs_set_str(handle, "ssid", wifi_ssid);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando wifi_ssid");
    err = nvs_set_str(handle, "pass", wifi_pass);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando wifi_pass");
    err = nvs_set_str(handle, "server", host_ip);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando wifi_server");
    err = nvs_set_i32(handle, "port", wifi_port);
    if(err != ESP_OK)
        ESP_LOGE(TAG, "Error guardando wifi_port");
    
    err = nvs_commit(handle);
    if(err != ESP_OK){
        ESP_LOGE(TAG, "Error al hacer commit en NVS (%s)", esp_err_to_name(err));
    }
    nvs_close(handle);
}

//Funcion para recuperar la configuracion inicial de la memoria flash
bool load_config(void){
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if(err != ESP_OK){
        ESP_LOGE(TAG, "Error abriendo NVS (%s)", esp_err_to_name(err));
        return false;
    }

    err = nvs_get_i32(handle, "id", &id);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    err = nvs_get_i32(handle, "piso", &piso);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    err = nvs_get_i32(handle, "limit", &limit_sound);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    err = nvs_get_i32(handle, "salon", &salon_p);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    size_t required_size = sizeof(tipo);
    err = nvs_get_str(handle, "tipo", tipo, &required_size);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    required_size = sizeof(area);
    err = nvs_get_str(handle, "area", area, &required_size);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    err = nvs_get_i32(handle, "flag", &flag_p);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    required_size = sizeof(wifi_ssid);
    err = nvs_get_str(handle, "ssid", wifi_ssid, &required_size);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    required_size = sizeof(wifi_pass);
    err = nvs_get_str(handle, "pass", wifi_pass, &required_size);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    required_size = sizeof(host_ip);
    err = nvs_get_str(handle, "server", host_ip, &required_size);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }
    err = nvs_get_i32(handle, "port", &wifi_port);
    if(err == ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "La clave no fue encontrada en NVS");
        nvs_close(handle);
        return false;
    }

    ESP_LOGI("TAG", "Configuracion actualizada: wifi_ssid=%s, wifi_pass=%s, wifi_server=%s, wifi_port=%d, id=%d, limit_sound=%d, piso=%d, tipo=%s, area=%s", wifi_ssid, wifi_pass, host_ip, wifi_port, id, limit_sound, piso, tipo, area);
    
    nvs_close(handle);
    return true;
}

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
    
    if(wifi_event_group == NULL){
        wifi_event_group = xEventGroupCreate();
    }
    
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, wifi_ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, wifi_pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

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

    char str_id[10], str_limit[10], str_piso[10], str_tipo[20], str_area[30], str_ssid[10], str_pass[10], str_serv[30], str_port[10];
    sscanf(buf, "ssid=%9[^&]&pass=%9[^&]&serv=%29[^&]&port=%9[^&]&id=%9[^&]&limit_sound=%9[^&]&piso=%9[^&]&tipo=%19[^&]&area=%29s", str_ssid, str_pass, str_serv, str_port, str_id, str_limit, str_piso, str_tipo, str_area);

    id = atoi(str_id);
    strncpy(wifi_ssid, str_ssid, sizeof(str_ssid) - 1);
    wifi_ssid[sizeof(wifi_ssid) - 1] = '\0';
    strncpy(wifi_pass, str_pass, sizeof(str_pass) - 1);
    wifi_pass[sizeof(wifi_pass) - 1] = '\0';
    strncpy(host_ip, str_serv, sizeof(str_serv) - 1);
    host_ip[sizeof(host_ip) - 1] = '\0';
    wifi_port = atoi(str_port);
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
    ESP_LOGI("HTTP_POST", "Configuracion actualizada: wifi_ssid=%s, wifi_pass=%s, wifi_server=%s, wifi_port=%d, id=%d, limit_sound=%d, piso=%d, tipo=%s, area=%s", wifi_ssid, wifi_pass, host_ip, wifi_port, id, limit_sound, piso, tipo, area);
    flag_r = 0;
    save_config();

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

//Tarea encargada de enviar comandos al servidor
static void send_task(void *pvParameters){
    int sound = 0;
    char rx_buffer[128], rx_buffer2[128];
    int rx_buffer3[sizeof(int) + sizeof(int)];
    sensor_packet_t packet, packet2;
    while(1){

        xSemaphoreTake(xMutex, portMAX_DELAY);
        
        prom = x / c;

        ESP_LOGI(TAG, "Promedio de sonido es: %" PRIu64, prom);

        if((prom < limit_sound) && (prom !=0)){
            sound = 1;
        }
        else
            sound = 0;

        memset(&packet, 0, sizeof(packet));
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
                    ESP_LOGI(TAG, "Se recibio un NACK");
                    int err2 = send(sock, "ACK", strlen("ACK"), 0);
                    if (err2 < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        break;
                    }
                    else{

                        int len3 = recv(sock, rx_buffer3, sizeof(rx_buffer3), 0);
                        if(len3 < 0){
                            ESP_LOGE(TAG, "recv failed: errno %d", errno);
                            break;
                        }
                        else if(len3 == 0){
                            ESP_LOGE(TAG, "Server cerro conexion");
                            break;
                        }
                        else{
                            ESP_LOGI(TAG, "Actualizando datos del sensor");
                            memcpy(&id, rx_buffer3, sizeof(id));
                            memcpy(&limit_sound, rx_buffer3 + sizeof(id), sizeof(limit_sound));
                            ESP_LOGI(TAG, "NUEVO VALOR DE ID ES: %d", id);
                            ESP_LOGI(TAG, "NUEVO VALOR DE LIMIT_SOUND ES: %d", limit_sound);
                            save_config();
                        }
                    }
                }
            }
        }

        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Pausa de 10s
    }
}
// http://192.168.137.218:8000
//una vez que funcione todo: HACER LA PRESENTACION
    //Fijarse en la estructura de la propuesta inicial en moodle
    //Poner un diagrama de conexiones en la presentacion

void tcp_client(void)
{
    if(load_config() == false){//Si no se encuentra configuracion inicial en memoria flash, se configura esp como AP y entramos a la pagina para ingresar datos
    
        //Entra a la pagina con http://192.168.4.1/
        wifi_init_softap();
        httpd_handle_t server = start_http_server();

        while(flag_r){
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }

        if(server){
            httpd_stop(server);
        }
    }

    int del = 1;
    while(del <= 110){
        ESP_LOGI(TAG_W, "Tiempo para conectar raspberry: %d / 110", del);
        del++;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    if(wifi_connect() == ESP_OK){
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
            dest_addr.sin_port = htons(wifi_port);
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
            ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, wifi_port);

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
