#include "common.h"

Cliente clientes[MAX_CLIENTES];
int num_clientes = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int servidor_online = 1;

void log_evento(const char *evento) {
    char timestamp[20];
    get_timestamp(timestamp);
    printf("[%s] %s\n", timestamp, evento);
    fflush(stdout);
}

void broadcast_lista_clientes() {
    if (!servidor_online) return;
    
    pthread_mutex_lock(&mutex); // Block lista
    
    char lista[sizeof(Cliente) * MAX_CLIENTES];
    int offset = 0;
    
    for (int i = 0; i < num_clientes; i++) {
        memcpy(lista + offset, &clientes[i], sizeof(Cliente));
        offset += sizeof(Cliente);
    }
    
    for (int i = 0; i < num_clientes; i++) {
        enviar_mensagem(clientes[i].socket, LIST, lista, sizeof(Cliente) * num_clientes);
    }
    
    pthread_mutex_unlock(&mutex);
}

void *manipular_cliente(void *arg) {
    int sock = *(int *)arg;
    free(arg);
    
    char buffer[TAM_MENSAGEM];
    int tipo, tamanho;
    Cliente novo_cliente;
    
    if (receber_mensagem(sock, &tipo, buffer, &tamanho) < 0 || tipo != JOIN) {
        close(sock);
        return NULL;
    }
    
    // Extrair informações do cliente (incluindo porta P2P)
    memcpy(&novo_cliente, buffer, sizeof(Cliente));
    novo_cliente.socket = sock;
    novo_cliente.p2p_socket = -1;
    
    struct sockaddr_in endereco_cliente;
    socklen_t tamanho_endereco = sizeof(endereco_cliente);
    getpeername(sock, (struct sockaddr*)&endereco_cliente, &tamanho_endereco);
    strcpy(novo_cliente.IP, inet_ntoa(endereco_cliente.sin_addr));
    
    // Log de conexão
    char log_msg[100];
    snprintf(log_msg, sizeof(log_msg), "Cliente conectado: %s (%s:%d) P2P:%d", 
             novo_cliente.nome, novo_cliente.IP, novo_cliente.porta, novo_cliente.p2p_port);
    log_evento(log_msg);
    
    pthread_mutex_lock(&mutex);
    if (num_clientes < MAX_CLIENTES) {
        clientes[num_clientes++] = novo_cliente;
    }
    pthread_mutex_unlock(&mutex);
    
    enviar_mensagem(sock, ACK, "Bem-vindo!", 11);
    broadcast_lista_clientes();
    
    while (servidor_online) {
        if (receber_mensagem(sock, &tipo, buffer, &tamanho) < 0) {
            break;
        }
        
        switch (tipo) {
            case DIRECT: {
                int destino_idx = *((int*)buffer);
                char *msg = buffer + sizeof(int);
                
                if (destino_idx >= 0 && destino_idx < num_clientes) {
                    // Log de mensagem direta
                    snprintf(log_msg, sizeof(log_msg), "Mensagem direta: %s -> %s: %s", 
                             novo_cliente.nome, clientes[destino_idx].nome, msg);
                    log_evento(log_msg);
                    
                    // Tentar enviar via servidor
                    if (enviar_mensagem(clientes[destino_idx].socket, MSG, msg, tamanho - sizeof(int)) < 0) {
                        log_evento("Falha ao enviar via servidor, notificando cliente");
                        enviar_mensagem(sock, P2P_REQUEST, (char*)&destino_idx, sizeof(int));
                    }
                }
                break;
            }
            case BROADCAST: {
                // Log de broadcast
                snprintf(log_msg, sizeof(log_msg), "Broadcast: %s: %s", novo_cliente.nome, buffer);
                log_evento(log_msg);
                
                for (int i = 0; i < num_clientes; i++) {
                    if (clientes[i].socket != sock) {
                        enviar_mensagem(clientes[i].socket, MSG, buffer, tamanho);
                    }
                }
                break;
            }
            case LEAVE: {
                goto sair;
            }
            case GET_LIST: {
                pthread_mutex_lock(&mutex);
                enviar_mensagem(sock, LIST, (char*)clientes, sizeof(Cliente) * num_clientes);
                pthread_mutex_unlock(&mutex);
                break;
            }
            case P2P_CONFIRM: {
                int destino_idx = *((int*)buffer);
                if (destino_idx >= 0 && destino_idx < num_clientes) {
                    enviar_mensagem(clientes[destino_idx].socket, P2P_REQUEST, (char*)&novo_cliente, sizeof(Cliente));
                }
                break;
            }
        }
    }
    
sair:
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < num_clientes; i++) {
        if (clientes[i].socket == sock) {
            // Log de desconexão
            snprintf(log_msg, sizeof(log_msg), "Cliente desconectado: %s (%s:%d)", 
                     clientes[i].nome, clientes[i].IP, clientes[i].porta);
            log_evento(log_msg);
            
            for (int j = i; j < num_clientes - 1; j++) {
                clientes[j] = clientes[j + 1];
            }
            num_clientes--;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
    
    broadcast_lista_clientes();
    close(sock);
    return NULL;
}

int main() {
#ifdef WIN
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("WSAStartup falhou");
        return 1;
    }
#endif

    int servidor = criar_socket(PORTA_SERVIDOR_TCP);
    if (servidor < 0) {
        return 1;
    }
    
    printf("Servidor iniciado na porta %d\n", PORTA_SERVIDOR_TCP);
    log_evento("Servidor iniciado");
    
    while (servidor_online) {
        struct sockaddr_in cliente_addr;
        socklen_t cliente_len = sizeof(cliente_addr);
        int *novo_socket = malloc(sizeof(int));
        
        *novo_socket = accept(servidor, (struct sockaddr*)&cliente_addr, &cliente_len);
        if (*novo_socket < 0) {
            perror("accept falhou");
            free(novo_socket);
            continue;
        }
        
        printf("Conexão recebida de %s:%d\n", 
               inet_ntoa(cliente_addr.sin_addr), 
               ntohs(cliente_addr.sin_port));
        
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, manipular_cliente, (void*)novo_socket) != 0) {
            perror("Falha ao criar thread");
            close(*novo_socket);
            free(novo_socket);
        }
        pthread_detach(thread_id);
    }
    
    close(servidor);
#ifdef WIN
    WSACleanup();
#endif
    return 0;
}