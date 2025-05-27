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

#define PORT 8250

struct timespec start_t;
int timer = 0;
//int id = 0;

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

	char query[350];
	if(flag)
		snprintf(query, sizeof(query), "INSERT INTO mediciones(id,piso,threshold_sound,tipo,salon) VALUES(%d,%d,%d,'%s',%d)", id,piso,limit,tipo,salon);
	else
		snprintf(query, sizeof(query), "INSERT INFO mediciones(id,piso,threshold_sound,tipo,area) VALUES(%d,%d,%d,'%s','%s')", id,piso,limit,tipo,area);

	printf("QUERY: %s\n", query);
	if(mysql_query(conn, query)){
		fprintf(stderr, "INSERT fallido. Error: %s\n", mysql_error(conn));
	}
	else{
		printf("INSERT exitoso\n");
	}

	mysql_close(conn);
}

void *server_task(void *arg) {
	int listen_sock, client_sock;
	struct sockaddr_in server_addr, client_addr;
	socklen_t addr_len = sizeof(client_addr);
	char rx_buffer[128];
	int keepAlive = 1;
	int keepIdle = 10;
	int keepInterval = 5;
	int keepCount = 3;

	if((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Error al crear el socket");
		pthread_exit(NULL);
	}

	int opt = 1;
	if(setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
		perror("Error en setsockopt");
		close(listen_sock);
		pthread_exit(NULL);
	}
	
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);

	if(bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){
		perror("Error en bind");
		close(listen_sock);
		pthread_exit(NULL);
	}

	if(listen(listen_sock, 1) < 0){
		perror("Error en listen");
		close(listen_sock);
		pthread_exit(NULL);
	}

	printf("Servidor escuchando en el puerto %d\n", PORT);

	if((client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len)) < 0){
		perror("Error al aceptar conexión");
		close(listen_sock);
		pthread_exit(NULL);
	}
	printf("Cliente conectado. \n");

	//uint64_t x;
	//uint32_t c;
	int sensor_id, sound;
	//uint64_t prom;
	char command[] = "DET_SOUND";
	//uint8_t buffer[sizeof(int) + sizeof(int)];
	sensor_packet_t packet;

	while(1){
		if(send(client_sock, command, strlen(command), 0) < 0){
			perror("Error al enviar DET_SOUND");
			break;
		}

		int len = recv(client_sock, &packet, sizeof(sensor_packet_t), 0);
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
			if(packet.sound){
				printf("Sonido detectado\n");
				if(!timer){
					clock_gettime(CLOCK_MONOTONIC, &start_t);
					timer = 1;
					//id = packet.id;
					printf("Enviando comando ON_LED\n");
					if(send(client_sock, "ON_LED", strlen("ON_LED"), 0) < 0){
						perror("Error al enviar ON_LED");
						break;
					}
				}
			}
			else{
				printf("No se detecta sonido\n");
				if(timer){
					struct timespec end_t;
					clock_gettime(CLOCK_MONOTONIC, &end_t);
					double total_t = (end_t.tv_sec - start_t.tv_sec) + (end_t.tv_nsec - start_t.tv_nsec) / 1e9;
					printf("Se detectó sonido continuo durante %.2f segundos.\n", total_t);
					timer = 0;
					printf("Enviando comando OFF_LED\n");
					if(send(client_sock, "OFF_LED", strlen("OFF_LED"), 0) < 0){
						perror("Error al enviar OFF_LED");
						break;
					}
					conn_db(packet.id,packet.piso,packet.limit_sound,packet.salon_p,packet.tipo,packet.area,packet.flag_p);
				}
			}
		}

		sleep(10);
	}
	
	close(client_sock);
	close(listen_sock);
	pthread_exit(NULL);
}

int main() {
	pthread_t tid;
	if(pthread_create(&tid, NULL, server_task, NULL) != 0){
		perror("Error creando el hilo del servidor");
		exit(EXIT_FAILURE);
	}

	pthread_join(tid, NULL);
	return 0;
}
