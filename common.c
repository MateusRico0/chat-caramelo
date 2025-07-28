#include "common.h"

int criar_socket(int porta) {
    int sock;
    struct sockaddr_in endereco;
    int opt = 1;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Erro na criação do socket");
        return -1;
    }

    // Permitir reutilização de porta
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt))) {
        perror("setsockopt falhou");
    }

    if (porta > 0) {
        memset(&endereco, 0, sizeof(endereco));
        endereco.sin_family = AF_INET;
        endereco.sin_addr.s_addr = htonl(INADDR_ANY);
        endereco.sin_port = htons(porta);

        if (bind(sock, (struct sockaddr *) &endereco, sizeof(endereco)) < 0) {
            perror("Erro no bind()");
            close(sock);
            return -1;
        }

        if (listen(sock, MAXPENDING) < 0) {
            perror("Erro no listen()");
            close(sock);
            return -1;
        }
    }

    return sock;
}

int conectar_com_servidor(int sock, char *IP, int porta) {
    struct sockaddr_in endereco;
    
    memset(&endereco, 0, sizeof(endereco));
    endereco.sin_family = AF_INET;
    endereco.sin_addr.s_addr = inet_addr(IP);
    endereco.sin_port = htons(porta);

    if (connect(sock, (struct sockaddr *) &endereco, sizeof(endereco)) < 0) {
        perror("Erro no connect()");
        return -1;
    }
    return 0;
}

int enviar_mensagem(int sock, int tipo, const char *conteudo, int tamanho) {
    Mensagem msg;
    msg.tipo = tipo;
    msg.tamanho = tamanho;
    memcpy(msg.conteudo, conteudo, tamanho);
    
    int tamanho_total = 8 + tamanho;
    if (send(sock, &msg, tamanho_total, 0) != tamanho_total) {
        perror("Erro no envio da mensagem");
        return -1;
    }
    return 0;
}

int receber_mensagem(int sock, int *tipo, char *conteudo, int *tamanho) {
    Mensagem msg;
    int bytes_recebidos = recv(sock, &msg, sizeof(Mensagem), 0);
    
    if (bytes_recebidos < 8) {
        return -1;
    }
    
    *tipo = msg.tipo;
    *tamanho = msg.tamanho;
    memcpy(conteudo, msg.conteudo, msg.tamanho);
    
    return 0;
}

void get_timestamp(char *buffer) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, 20, "%H:%M:%S", t);
}

int tentar_conexao_direta(Cliente *destino) {
    if (destino->p2p_socket > 0) {
        return destino->p2p_socket; // Conexão já existe
    }
    
    // Tentar conectar diretamente
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Erro ao criar socket P2P");
        return -1;
    }
    
    struct sockaddr_in endereco;
    memset(&endereco, 0, sizeof(endereco));
    endereco.sin_family = AF_INET;
    endereco.sin_addr.s_addr = inet_addr(destino->IP);
    endereco.sin_port = htons(destino->p2p_port);
    
    if (connect(sock, (struct sockaddr *)&endereco, sizeof(endereco)) < 0) {
        close(sock);
        return -1;
    }
    
    destino->p2p_socket = sock;
    return sock;
}

int enviar_mensagem_direta(int sock, const char *mensagem) {
    int len = strlen(mensagem) + 1;
    if (send(sock, mensagem, len, 0) != len) {
        return -1;
    }
    return 0;
}