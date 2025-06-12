#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>
#include <stdint.h>
#include <mysql/mysql.h>
#include "mongoose/mongoose.h"

#define PORT 8250

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static const char *http_port = "8000";

//Estructura de paquetes tcp para enviar o recibir
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

//Funcion para conectar e insertar a la base de datos (Los registros se pueden ver en una pagina web)
void conn_db(int id, int piso, int limit, int salon, char *tipo, char *area, int flag){
	MYSQL *conn;
	char *server	= "localhost";
	char *user	= "alexm";
	char *password	= "dimentio2019";
	char *database	= "proyecto_db2";

	conn = mysql_init(NULL);
	if(conn == NULL){
		fprintf(stderr, "mysql_init() da error\n");
		return;
	}

	if(mysql_real_connect(conn, server, user, password, database, 0, NULL, 0) == NULL){
		fprintf(stderr, "mysql_real_connect() da error\nError: %s\n", mysql_error(conn));
		mysql_close(conn);
		return;
	}

	//Con un query INSERT se insertan (je) las mediciones y datos importantes del sensor en la base de datos
	char query[350];
	if(flag)
		snprintf(query, sizeof(query), "INSERT INTO mediciones(id,piso,threshold_sound,tipo,salon) VALUES(%d,%d,%d,'%s',%d)", id,piso,limit,tipo,salon);
	else
		snprintf(query, sizeof(query), "INSERT INTO mediciones(id,piso,threshold_sound,tipo,area) VALUES(%d,%d,%d,'%s','%s')", id,piso,limit,tipo,area);

	printf("QUERY: %s\n", query);
	if(mysql_query(conn, query)){
		fprintf(stderr, "INSERT fallido. Error: %s\n", mysql_error(conn));
	}
	else{
		printf("INSERT exitoso\n");
	}

	mysql_close(conn);
}

sensor_packet_t backup1, backup2, backup3;

//Funcion para ingresar datos de los clientes en la pagina web
void backup_json(char *buffer, size_t buf_len){
	pthread_mutex_lock(&mutex);
	snprintf(buffer, buf_len,
		"{"
			"\"backup1\":{"
				"\"id\":%d,"
				"\"limit_sound\":%d,"
				"\"piso\":%d,"
				"\"tipo\":\"%s\","
				"\"area\":\"%s\""
			"},"
			"\"backup2\":{"
				"\"id\":%d,"
				"\"limit_sound\":%d,"
				"\"piso\":%d,"
				"\"tipo\":\"%s\","
				"\"area\":\"%s\""
			"},"
			"\"backup3\":{"
				"\"id\":%d,"
				"\"limit_sound\":%d,"
				"\"piso\":%d,"
				"\"tipo\":\"%s\","
				"\"area\":\"%s\""
			"}"
		"}",
		backup1.id, backup1.limit_sound, backup1.piso, backup1.tipo, backup1.area,
		backup2.id, backup2.limit_sound, backup2.piso, backup2.tipo, backup2.area,
		backup3.id, backup3.limit_sound, backup3.piso, backup3.tipo, backup3.area);

	pthread_mutex_unlock(&mutex);
}

//Manejador de eventos de la pagina web
static void http_ev_handler(struct mg_connection *conn, int ev, void *ev_data){
	printf("*****ENTRANDO AL EVENT_HANDLER***** \n");
	if(ev == MG_EV_HTTP_MSG){
		printf("\nEL EVENTO APARENTEMENTE ES MG_EV_HTTP_MSG\n");
		struct mg_http_message *hm = (struct mg_http_message *) ev_data;

		printf("************************************\n");
		printf("Metodo: %.*s \n", (int)hm->method.len, hm->method.buf);
		printf("URI: %.*s \n", (int)hm->uri.len, hm->uri.buf);
		printf("Query: %.*s \n", (int)hm->query.len, hm->query.buf);
		printf("Cuerpo: %.*s \n", (int)hm->body.len, hm->body.buf);
		printf("************************************\n");

		//Usuario entra a la pagina
		if(mg_strcmp(hm->uri, mg_str("/")) == 0){
			printf("\nENTRANDO AL INDEX\n");
			//La estrucuctura de la pagina se saca de esta ruta
			struct mg_http_serve_opts opts = {.root_dir = "/home/alexm/proyectos/web_p"};
			mg_http_serve_dir(conn, hm, &opts);
			printf("\nYA SE ENTRO AL INDEX XD\n");
			return;
		}
		//Pagina se actualiza con los valores actuales de los backups (clientes conectados)
		if(mg_strcmp(hm->uri, mg_str("/backup")) == 0){
			printf("\nAQUI SE ENTRO AL /BACKUP \n");
			char json_resp[512];
			backup_json(json_resp, sizeof(json_resp));
			mg_http_reply(conn, 200, "Content-Type: application/json\r\n", "%s", json_resp);
			return;
		}
		//Usuario ingresa nuevos datos para un cliente
		else if(mg_strcmp(hm->uri, mg_str("/update")) == 0){
			char backup_sel[10], new_id[10], new_limit[10];
			if(mg_http_get_var(&hm->query, "backup", backup_sel, sizeof(backup_sel)) <= 0 || mg_http_get_var(&hm->query, "id", new_id, sizeof(new_id)) <= 0 || mg_http_get_var(&hm->query, "limit", new_limit, sizeof(new_limit)) <= 0){
				mg_http_reply(conn, 400, "", "Parametros invalidos\n");
				return;
			}
			int backup_index = atoi(backup_sel);
			int id_val = atoi(new_id);
			int limit_val = atoi(new_limit);

			pthread_mutex_lock(&mutex);

			if(backup_index == 1){
				backup1.id = id_val;
				backup1.limit_sound = limit_val;
			}
			else if(backup_index == 2){
				backup2.id = id_val;
				backup2.limit_sound = limit_val;
			}
			else if(backup_index == 3){
				backup3.id = id_val;
				backup3.limit_sound = limit_val;
			}
			pthread_mutex_unlock(&mutex);

			mg_http_reply(conn, 200, "", "Actualizacion exitosa\n");
			return;
		}
		else{
			mg_http_reply(conn, 404, "", "Not Found");
		}
	}
}

//Funcion para inicializar pagina web
void *http_server(void *arg){
	struct mg_mgr mgr;
	mg_mgr_init(&mgr);
	
	struct mg_connection *conn = mg_http_listen(&mgr, "http://0.0.0.0:8000", (mg_event_handler_t)http_ev_handler, NULL);
	if(conn == NULL){
		fprintf(stderr, "Error iniciando el servidor HTTP en el puerto 8000\n");
		return NULL;
	}
	printf("\n\n LISTENER INICIADO PARA EL HTTP \n\n");
	while(1){
		mg_mgr_poll(&mgr, 1000);
	}
	mg_mgr_free(&mgr);
	return NULL;
}

//Funcion para comunicarse con clientes
void *server_task(void *arg) {
	
	int client_s = *((int *)arg);
	free(arg);
	
	sensor_packet_t packet;
	struct timespec start_t;
	int timer = 0;
	int flag = 0;
	int s = 0;
	int buf[sizeof(int) + sizeof(int)];
	char buffer[128];

	while(1){

		int len = recv(client_s, &packet, sizeof(sensor_packet_t), 0);
		if(len < 0){
			perror("Error al recibir datos");
			break;
		}
		else if(len == 0){
			printf("Conexión cerrada por el cliente\n");
			break;
		}
		else if(len != sizeof(sensor_packet_t)){
			printf("Paquete recibido de tamanio incorrecto (se recibieron %d bytes, se esperaban %lu bytes)\n", len, sizeof(sensor_packet_t));
			break;
		}
		else{
			printf("Indicador de sonido recibido: %d\n", packet.sound);
			printf("ID de sensor es: %d\n", packet.id);
			printf("El threshold de sonido es: %d\n", packet.limit_sound);
			printf("El piso del sensor es: %d\n", packet.piso);
			printf("El tipo del sensor es: %s\n", packet.tipo);
			printf("La direccion mac del sensor es: %02x:%02x:%02x:%02x:%02x:%02x\n", packet.mac[0],packet.mac[1],packet.mac[2],packet.mac[3],packet.mac[4],packet.mac[5]);

			//Se decide como almacenar el paquete
			pthread_mutex_lock(&mutex);
			//Se verifica si el paquete recibido proviene de un cliente registrado en los backups
			if(memcmp(backup1.mac, packet.mac, sizeof(packet.mac)) == 0){//Si esta registrado en backup1
				//s = packet.sound;
				//Vemos si los datos son diferentes a los almacenados
				//if(memcmp(&backup1, &packet, sizeof(sensor_packet_t)) != 0){
				if(backup1.id != packet.id){
					//Vemos si el cliente perdio sus datos
					//if(packet.id != 0)
					//	packet = backup1;// CAMBIAR   Cliente tiene datos, actualizamos backup
					//else{
						packet = backup1;//Cliente no tiene datos
						memcpy(buf, &packet.id, sizeof(packet.id));
						memcpy(buf + sizeof(packet.id), &packet.limit_sound, sizeof(packet.limit_sound));
						//packet.sound = s;	//ES SOLO DE PRUEBA
						flag = 1;	//Activamos bandera para restaurarlo
					//}
				}
			}
			else if(memcmp(backup2.mac, packet.mac, sizeof(packet.mac)) == 0){//Si esta registrado en backup2
				//s = packet.sound;
				//Vemos si los datos son diferentes a los almacenados
				//if(memcmp(&backup2, &packet, sizeof(sensor_packet_t)) != 0){
				if(backup2.id != packet.id){
					//Vemos si el cliente perdio sus datos
					//if(packet.id != 0)
					//	backup2 = packet;// CAMBIAR    Cliente tiene datos, actualizamos backup
					//else{
						packet = backup2;//Cliente no tiene datos
						memcpy(buf, &packet.id, sizeof(packet.id));
						memcpy(buf + sizeof(packet.id), &packet.limit_sound, sizeof(packet.limit_sound));
				//		packet.sound = s;	//SOLO PRUEBA
						flag = 1;	//Activamos bandera para restaurarlo
					//}
				}
			}
			else if(memcmp(backup3.mac, packet.mac, sizeof(packet.mac)) == 0){//Si esta registrado en backup3
				//s = packet.sound;
				//Vemos si los datos son diferentes a los almacenados
				//if(memcmp(&backup3, &packet, sizeof(sensor_packet_t)) != 0){
				if(backup3.id != packet.id){
					//Vemos si el cliente perdio sus datos
					//if(packet.id != 0)
					//	backup3 = packet;// CAMBIAR    Cliente tiene datos, actualizamos backup
					//else{
						packet = backup3;//Cliente no tiene datos
						memcpy(buf, &packet.id, sizeof(packet.id));
						memcpy(buf + sizeof(packet.id), &packet.limit_sound, sizeof(packet.limit_sound));
				//		packet.sound = s;	//SOLO PRUEBA
						flag = 1;	//Activamos bandera para restaurarlo
					//}
				}
			}
			else{//Si no esta registrado (nuevo cliente), se almacena en un backup disponible
				if(backup1.id == 0){
					backup1 = packet;
				}
				else if(backup2.id == 0){
					backup2 = packet;
				}
				else if(backup3.id == 0){
					backup3 = packet;
				}
			}
			pthread_mutex_unlock(&mutex);

			if(packet.sound){
				printf("Sonido detectado\n");
				if(!timer){//Si se detecta sonido despues de un silencio
					clock_gettime(CLOCK_MONOTONIC, &start_t);
					timer = 1;
					//id = packet.id;
					printf("Enviando comando ON_LED\n");
					if(send(client_s, "ON_LED", strlen("ON_LED"), 0) < 0){
						perror("Error al enviar ON_LED");
						break;
					}
				}
				else{//Se ha detectado sonido y aun sigue
					if(send(client_s, "PASS", strlen("PASS"), 0) < 0){
						perror("Error con enviado de comando generico");
						break;
					}
				}
			}
			else{
				printf("No se detecta sonido\n");
				if(timer){ //Se detecta que el sonido se detuvo
					struct timespec end_t;
					clock_gettime(CLOCK_MONOTONIC, &end_t);
					double total_t = (end_t.tv_sec - start_t.tv_sec) + (end_t.tv_nsec - start_t.tv_nsec) / 1e9;
					printf("Se detectó sonido continuo durante %.2f segundos.\n", total_t);
					timer = 0;
					printf("Enviando comando OFF_LED\n");
					if(send(client_s, "OFF_LED", strlen("OFF_LED"), 0) < 0){
						perror("Error al enviar OFF_LED");
						break;
					}
					conn_db(packet.id,packet.piso,packet.limit_sound,packet.salon_p,packet.tipo,packet.area,packet.flag_p);
				}
				else{//No se ha detectado sonido anteriormente
					if(send(client_s, "PASS", strlen("PASS"), 0) < 0){
						perror("Error con enviado de comando generico");
						break;
					}
				}
			}
			//Si la bander esta activa enviamos al cliente un NACK que indica que debe recibir sus datos perdidos del servidor
			if(flag == 0){
				if(send(client_s, "ACK", strlen("ACK"), 0) < 0){
					perror("Error con enviado de ACK");
					break;
				}
			}
			else{
				if(send(client_s, "NACK", strlen("NACK"), 0) < 0){
					perror("Error con enviado de NACK");
					break;
				}
				else{
					int len_r = recv(client_s, buffer, sizeof(buffer) - 1, 0);
					if(len_r < 0){
						perror("Error recibiendo datos");
						break;
					}
					else{
						buffer[len_r] = 0;
						printf("Se recibio un: %s \n", buffer);

						if(strcmp(buffer, "ACK") == 0){
							printf("Enviando datos actualizados al cliente\n");
							if(send(client_s, buf, sizeof(buf), 0) < 0){
								perror("Error con el enviado de datos de restauracion");
								break;
							}
							flag = 0;
						}
						else
							printf("WHAT xd \n");
					}
				}
			}
		}

		sleep(2);	//Delay de 2s
	}
	
	close(client_s);
	pthread_exit(NULL);
}

//Funcion principal
int main() {
	
	//sensor_packet_t backup1, backup2, backup3;
	int listen_sock;
	struct sockaddr_in server_addr, client_addr;
	socklen_t addr_len = sizeof(client_addr);
	pthread_t http_tid, tid;

	//Crea hilo para manejar pagina web
	if(pthread_create(&http_tid, NULL, http_server, NULL) != 0){
		perror("Error al crear el hilo del servidor HTTP");
		exit(EXIT_FAILURE);
	}

	//Creando socket para escuchar
	if((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Error al crear el socket");
		exit(EXIT_FAILURE);
	}

	int opt = 1;
	if(setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
		perror("Error al crear el socket");
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);

	if(bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		perror("Error en bind");
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	//Socket comienza a escuchar dispositivos (maximo 3)
	if(listen(listen_sock, 3) < 0){
		perror("Error en listen");
		close(listen_sock);
		exit(EXIT_FAILURE);
	}

	printf("Servidor escuchando en el puerto %d\n", PORT);

	while(1){
		int *client_sock = malloc(sizeof(int));//Se aloja espacio en memoria para un nuevo socket
		if(client_sock == NULL){
			perror("Error en asignacion de memoria");
			continue;
		}
		//Si se detecta un dispositivo se acepta la conexion y se le da el socket alojado en memoria
		*client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
		if(*client_sock < 0){
			perror("Error al aceptar conexion");
			free(client_sock);
			continue;
		}
		printf("Nuevo cliente conectado.\n");

		//Se crea un hilo para la comunicacion con ese socket especifico
		if(pthread_create(&tid, NULL, server_task, client_sock) != 0){
			perror("Error al crear hilo para el cliente");
			free(client_sock);
			continue;
		}
		pthread_detach(tid);
	}

	close(listen_sock);

	return 0;
}
