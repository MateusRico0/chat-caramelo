// ================ client.c ================
#include "common.h"
#include <ncurses.h>

#define MAX_HISTORICO 100
#define MAX_DIRETAS 10

typedef struct {
    char remetente[30];
    char mensagem[TAM_MENSAGEM];
    int enviada; // 0 = recebida, 1 = enviada
} HistoricoMensagem;

typedef struct {
    Cliente cliente;
    int socket;
} ConexaoDireta;

WINDOW *win_lista, *win_chat, *win_input;
pthread_mutex_t mutex_tela = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_conexoes = PTHREAD_MUTEX_INITIALIZER;
int socket_cliente;
Cliente *lista_clientes = NULL;
int num_lista_clientes = 0;
HistoricoMensagem historico[MAX_HISTORICO];
int num_historico = 0;
char meu_nome[30];
int minha_porta_p2p;
int socket_p2p = -1;
ConexaoDireta conexoes_diretas[MAX_DIRETAS];
int num_conexoes_diretas = 0;

void atualizar_lista_clientes(char *dados, int tamanho) {
    num_lista_clientes = tamanho / sizeof(Cliente);
    lista_clientes = realloc(lista_clientes, tamanho);
    memcpy(lista_clientes, dados, tamanho);
    
    pthread_mutex_lock(&mutex_tela);
    wclear(win_lista);
    wprintw(win_lista, "Clientes conectados (%d):\n", num_lista_clientes);
    for (int i = 0; i < num_lista_clientes; i++) {
        wprintw(win_lista, " %d: %s [P2P:%d]\n", i, lista_clientes[i].nome, lista_clientes[i].p2p_port);
    }
    wrefresh(win_lista);
    pthread_mutex_unlock(&mutex_tela);
}

void adicionar_historico(const char *remetente, const char *msg, int enviada) {
    if (num_historico >= MAX_HISTORICO) {
        for (int i = 0; i < MAX_HISTORICO - 1; i++) {
            historico[i] = historico[i + 1];
        }
        num_historico--;
    }
    
    strncpy(historico[num_historico].remetente, remetente, 29);
    historico[num_historico].remetente[29] = '\0';
    strncpy(historico[num_historico].mensagem, msg, TAM_MENSAGEM - 1);
    historico[num_historico].mensagem[TAM_MENSAGEM - 1] = '\0';
    historico[num_historico].enviada = enviada;
    num_historico++;
    
    // Atualizar janela de chat
    pthread_mutex_lock(&mutex_tela);
    wclear(win_chat);
    for (int i = 0; i < num_historico; i++) {
        if (historico[i].enviada) {
            wattron(win_chat, COLOR_PAIR(2)); // Cor para mensagens enviadas
            wprintw(win_chat, "Você para %s: %s\n", historico[i].remetente, historico[i].mensagem);
            wattroff(win_chat, COLOR_PAIR(2));
        } else {
            wattron(win_chat, COLOR_PAIR(1)); // Cor para mensagens recebidas
            wprintw(win_chat, "%s: %s\n", historico[i].remetente, historico[i].mensagem);
            wattroff(win_chat, COLOR_PAIR(1));
        }
    }
    wrefresh(win_chat);
    pthread_mutex_unlock(&mutex_tela);
}

void *thread_receber(void *arg) {
    char buffer[TAM_MENSAGEM];
    int tipo, tamanho;
    
    while (1) {
        if (receber_mensagem(socket_cliente, &tipo, buffer, &tamanho) < 0) {
            // Servidor pode estar offline
            break;
        }
        
        buffer[tamanho] = '\0'; // Garantir terminação
        
        switch (tipo) {
            case ACK:
                pthread_mutex_lock(&mutex_tela);
                mvwprintw(win_input, 0, 0, "Servidor: %.*s", tamanho, buffer);
                wrefresh(win_input);
                pthread_mutex_unlock(&mutex_tela);
                break;
                
            case LIST:
                atualizar_lista_clientes(buffer, tamanho);
                break;
                
            case MSG:
                adicionar_historico("", buffer, 0);
                break;
                
            case P2P_REQUEST: {
                // Solicitação para conectar diretamente
                Cliente *origem = (Cliente*)buffer;
                
                // Tentar conectar diretamente
                int sock = tentar_conexao_direta(origem);
                if (sock > 0) {
                    // Confirmar conexão
                    enviar_mensagem(socket_cliente, P2P_CONFIRM, (char*)origem, sizeof(Cliente));
                    
                    // Adicionar à lista de conexões diretas
                    pthread_mutex_lock(&mutex_conexoes);
                    if (num_conexoes_diretas < MAX_DIRETAS) {
                        conexoes_diretas[num_conexoes_diretas].cliente = *origem;
                        conexoes_diretas[num_conexoes_diretas].socket = sock;
                        num_conexoes_diretas++;
                    }
                    pthread_mutex_unlock(&mutex_conexoes);
                }
                break;
            }
        }
    }
    return NULL;
}

void *thread_p2p_listener(void *arg) {
    int p2p_socket = *(int*)arg;
    
    while (1) {
        struct sockaddr_in cliente_addr;
        socklen_t cliente_len = sizeof(cliente_addr);
        int sock = accept(p2p_socket, (struct sockaddr*)&cliente_addr, &cliente_len);
        if (sock < 0) {
            continue;
        }
        
        // Receber mensagem
        char buffer[TAM_MENSAGEM];
        int bytes_recv = recv(sock, buffer, TAM_MENSAGEM - 1, 0);
        if (bytes_recv > 0) {
            buffer[bytes_recv] = '\0';
            // extrair nome da messagem
            char *colon = strchr(buffer, ':');
            char sender[30] = "(DIRETO)";
            char actual_msg[TAM_MENSAGEM];
            
            if (colon) {
                strncpy(sender, buffer, colon - buffer);
                sender[colon - buffer] = '\0';
                strcpy(actual_msg, colon + 2); // Pula ","  e espço
            } else {
                strcpy(actual_msg, buffer);
            }
            
            adicionar_historico(sender, actual_msg, 0);
        }
        close(sock);
    }
    return NULL;
}

int enviar_mensagem_direta_fallback(int idx, const char *msg) {
    if (idx < 0 || idx >= num_lista_clientes) {
        return -1;
    }
    
    // Verificar se já temos conexão direta
    pthread_mutex_lock(&mutex_conexoes);
    for (int i = 0; i < num_conexoes_diretas; i++) {
        if (strcmp(conexoes_diretas[i].cliente.nome, lista_clientes[idx].nome) == 0) {
            // Formatar messagem como "Nome: mensagem"
            char full_msg[TAM_MENSAGEM];
            snprintf(full_msg, sizeof(full_msg), "%s: %s", meu_nome, msg);
            
            int resultado = enviar_mensagem_direta(conexoes_diretas[i].socket, full_msg);
            pthread_mutex_unlock(&mutex_conexoes);
            return resultado;
        }
    }
    pthread_mutex_unlock(&mutex_conexoes);
    
    // Tentar nova conexão
    int sock = tentar_conexao_direta(&lista_clientes[idx]);
    if (sock > 0) {
        // Adicionar à lista de conexões diretas
        pthread_mutex_lock(&mutex_conexoes);
        if (num_conexoes_diretas < MAX_DIRETAS) {
            conexoes_diretas[num_conexoes_diretas].cliente = lista_clientes[idx];
            conexoes_diretas[num_conexoes_diretas].socket = sock;
            num_conexoes_diretas++;
        }
        pthread_mutex_unlock(&mutex_conexoes);
        
        // Formatar messagem como "Nome: mensagem"
        char full_msg[TAM_MENSAGEM];
        snprintf(full_msg, sizeof(full_msg), "%s: %s", meu_nome, msg);
        
        return enviar_mensagem_direta(sock, full_msg);
    }
    
    return -1;
}

void mostrar_ajuda() {
    pthread_mutex_lock(&mutex_tela);
    wclear(win_chat);
    wprintw(win_chat, "Comandos disponíveis:\n");
    wprintw(win_chat, " /lista        - Atualizar lista de clientes\n");
    wprintw(win_chat, " /msg <indice> <msg> - Enviar mensagem via servidor\n");
    wprintw(win_chat, " /p2p <indice> <msg> - Enviar mensagem direta (P2P)\n");
    wprintw(win_chat, " /bc <msg>     - Enviar broadcast\n");
    wprintw(win_chat, " /sair         - Desconectar\n");
    wprintw(win_chat, " /ajuda        - Mostrar esta ajuda\n");
    wprintw(win_chat, "\nPressione qualquer tecla para continuar...");
    wrefresh(win_chat);
    getch();
    wclear(win_chat);
    for (int i = 0; i < num_historico; i++) {
        if (historico[i].enviada) {
            wattron(win_chat, COLOR_PAIR(2));
            wprintw(win_chat, "Você para %s: %s\n", historico[i].remetente, historico[i].mensagem);
            wattroff(win_chat, COLOR_PAIR(2));
        } else {
            wattron(win_chat, COLOR_PAIR(1));
            wprintw(win_chat, "%s: %s\n", historico[i].remetente, historico[i].mensagem);
            wattroff(win_chat, COLOR_PAIR(1));
        }
    }
    wrefresh(win_chat);
    pthread_mutex_unlock(&mutex_tela);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Uso: %s <IP Servidor> <Nome>\n", argv[0]);
        return 1;
    }
    
    strncpy(meu_nome, argv[2], 29);
    meu_nome[29] = '\0';
    
#ifdef WIN
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("WSAStartup falhou");
        return 1;
    }
#endif

    // Criar socket P2P para conexões diretas
    minha_porta_p2p = PORTA_P2P_BASE + (getpid() % 1000);
    socket_p2p = criar_socket(minha_porta_p2p);
    if (socket_p2p < 0) {
        // Tentar porta aleatória
        socket_p2p = criar_socket(0);
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if (getsockname(socket_p2p, (struct sockaddr*)&addr, &len) == 0) {
            minha_porta_p2p = ntohs(addr.sin_port);
        } else {
            minha_porta_p2p = 0;
        }
    }
    
    if (socket_p2p < 0) {
        fprintf(stderr, "Falha ao criar socket P2P\n");
        return 1;
    }
    
    // Iniciar thread para ouvir conexões diretas
    pthread_t thread_p2p;
    pthread_create(&thread_p2p, NULL, thread_p2p_listener, &socket_p2p);
    pthread_detach(thread_p2p);
    
    socket_cliente = criar_socket(0);
    if (socket_cliente < 0) {
        return 1;
    }
    
    if (conectar_com_servidor(socket_cliente, argv[1], PORTA_SERVIDOR_TCP) < 0) {
        close(socket_cliente);
        return 1;
    }
    
    // Preparar JOIN com informações P2P
    Cliente meu_cliente;
    strncpy(meu_cliente.nome, meu_nome, 29);
    meu_cliente.nome[29] = '\0';
    meu_cliente.p2p_port = minha_porta_p2p;
    meu_cliente.p2p_socket = -1;
    
    // Enviar JOIN
    enviar_mensagem(socket_cliente, JOIN, (char*)&meu_cliente, sizeof(Cliente));
    
    // ============================================================== ncurses ==============================================================
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE); // Habilita teclas específicas como setas...
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK); // Mensagens recebidas
    init_pair(2, COLOR_CYAN, COLOR_BLACK);  // Mensagens enviadas
    init_pair(3, COLOR_YELLOW, COLOR_BLACK); // Mensagens diretas
    
    // tamanho das janelas
    int altura_lista = LINES / 4;
    int altura_chat = LINES - altura_lista - 3;
    int altura_input = 3;
    
    // Janelas
    win_lista = newwin(altura_lista, COLS, 0, 0);
    win_chat = newwin(altura_chat, COLS, altura_lista, 0);
    win_input = newwin(altura_input, COLS, altura_lista + altura_chat, 0);
    

    // Sroll e bordas
    scrollok(win_chat, TRUE);
    box(win_lista, 0, 0);
    box(win_chat, 0, 0);
    box(win_input, 0, 0);
    
    wrefresh(win_lista);
    wrefresh(win_chat);
    wrefresh(win_input);
    
    // Titulos para cada borda
    mvwprintw(win_lista, 0, 2, " Lista de Clientes ");
    mvwprintw(win_chat, 0, 2, " Conversa ");
    mvwprintw(win_input, 0, 2, " Comandos (P2P:%d) ", minha_porta_p2p);
    
    // ============================================================== ncurses ==============================================================

    // Thread para receber mensagens do servidor
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, thread_receber, NULL);
    pthread_detach(thread_id);
    
    // Solicitar lista inicial
    enviar_mensagem(socket_cliente, GET_LIST, "", 0);
    
    // Loop principal de entrada
    char entrada[256];
    while (1) {
        wmove(win_input, 1, 1);
        wclrtoeol(win_input);
        wprintw(win_input, "> ");
        wrefresh(win_input);
        
        echo();
        wgetnstr(win_input, entrada, sizeof(entrada) - 1);
        noecho();
        
        if (strcmp(entrada, "/lista") == 0) {
            enviar_mensagem(socket_cliente, GET_LIST, "", 0);
        } 
        else if (strncmp(entrada, "/msg", 4) == 0) {
            int idx;
            char *msg = strchr(entrada + 5, ' ');
            if (msg) {
                *msg = '\0';
                idx = atoi(entrada + 5);
                msg++;
                
                if (idx >= 0 && idx < num_lista_clientes) {
                    char buffer[sizeof(int) + 256];
                    memcpy(buffer, &idx, sizeof(int));
                    strcpy(buffer + sizeof(int), msg);
                    
                    enviar_mensagem(socket_cliente, DIRECT, buffer, sizeof(int) + strlen(msg) + 1);
                    adicionar_historico(lista_clientes[idx].nome, msg, 1);
                } else {
                    pthread_mutex_lock(&mutex_tela);
                    mvwprintw(win_input, 1, 1, "Índice inválido!");
                    wrefresh(win_input);
                    pthread_mutex_unlock(&mutex_tela);
                    napms(2000);
                }
            }
        } 
        else if (strncmp(entrada, "/p2p", 4) == 0) {
            int idx;
            char *msg = strchr(entrada + 5, ' ');
            if (msg) {
                *msg = '\0';
                idx = atoi(entrada + 5);
                msg++;
                
                if (idx >= 0 && idx < num_lista_clientes) {
                    if (enviar_mensagem_direta_fallback(idx, msg) == 0) {
                        // Sucesso
                        adicionar_historico(lista_clientes[idx].nome, msg, 1);
                        
                        pthread_mutex_lock(&mutex_tela);
                        wattron(win_chat, COLOR_PAIR(3)); // Yellow for P2P
                        wprintw(win_chat, "Você (P2P) para %s: %s\n", 
                                lista_clientes[idx].nome, msg);
                        wattroff(win_chat, COLOR_PAIR(3));
                        wrefresh(win_chat);
                        pthread_mutex_unlock(&mutex_tela);
                    } else {
                        pthread_mutex_lock(&mutex_tela);
                        mvwprintw(win_input, 1, 1, "Falha no P2P! Tentando via servidor...");
                        wrefresh(win_input);
                        pthread_mutex_unlock(&mutex_tela);
                        napms(2000);
                        
                        // Fallback para servidor
                        char buffer[sizeof(int) + 256];
                        memcpy(buffer, &idx, sizeof(int));
                        strcpy(buffer + sizeof(int), msg);
                        enviar_mensagem(socket_cliente, DIRECT, buffer, sizeof(int) + strlen(msg) + 1);
                        adicionar_historico(lista_clientes[idx].nome, msg, 1);
                    }
                } else {
                    pthread_mutex_lock(&mutex_tela);
                    mvwprintw(win_input, 1, 1, "Índice inválido!");
                    wrefresh(win_input);
                    pthread_mutex_unlock(&mutex_tela);
                    napms(2000);
                }
            } else {
                pthread_mutex_lock(&mutex_tela);
                mvwprintw(win_input, 1, 1, "Uso: /p2p <indice> <mensagem>");
                wrefresh(win_input);
                pthread_mutex_unlock(&mutex_tela);
                napms(2000);
            }
        }
        else if (strncmp(entrada, "/bc", 3) == 0) {
            char *msg = entrada + 4;
            if (*msg) {
                enviar_mensagem(socket_cliente, BROADCAST, msg, strlen(msg) + 1);
                adicionar_historico("Todos", msg, 1);
            }
        } 
        else if (strcmp(entrada, "/ajuda") == 0) {
            mostrar_ajuda();
        }
        else if (strcmp(entrada, "/sair") == 0) {
            enviar_mensagem(socket_cliente, LEAVE, "", 0);
            break;
        }
        else if (*entrada) {
            pthread_mutex_lock(&mutex_tela);
            mvwprintw(win_input, 1, 1, "Comando inválido! Digite /ajuda para ajuda");
            wrefresh(win_input);
            pthread_mutex_unlock(&mutex_tela);
            napms(2000);
        }
    }
    
    endwin();
    close(socket_cliente);
    close(socket_p2p);
#ifdef WIN
    WSACleanup();
#endif
    return 0;
}