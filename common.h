#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#ifdef WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#define TAM_MENSAGEM 1024
#define PORTA_SERVIDOR_TCP 9999
#define PORTA_P2P_BASE 10000
#define MAXPENDING 5
#define MAX_CLIENTES 100
#define MAX_TENTATIVAS 3 // REM

// Tipos de mensagem
#define JOIN 1
#define LIST 2
#define DIRECT 3
#define BROADCAST 4
#define LEAVE 5
#define ACK 6
#define MSG 7
#define GET_LIST 8
#define P2P_REQUEST 9
#define P2P_CONFIRM 10
#define P2P_MESSAGE 11

typedef struct cliente {
    char nome[30];
    char IP[22]; 
    short porta;
    int socket;
    int p2p_port; // Porta para conexões diretas
    int p2p_socket; // Socket para conexões diretas
} Cliente;

typedef struct mensagem {
    int tipo;
    int tamanho;
    char conteudo[TAM_MENSAGEM - 8]; // 1024 - 8 (tipo + tamanho)
} Mensagem;

// Funções de rede
int criar_socket(int porta);
int conectar_com_servidor(int sock, char *IP, int porta);
int enviar_mensagem(int sock, int tipo, const char *conteudo, int tamanho);
int receber_mensagem(int sock, int *tipo, char *conteudo, int *tamanho);
void get_timestamp(char *buffer);
int tentar_conexao_direta(Cliente *destino);
int enviar_mensagem_direta(int sock, const char *mensagem);

#endif