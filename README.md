# chat-caramelo

This project implements a simple chat application that supports both server-based messaging and direct peer-to-peer (P2P) communication.

## How to Compile:
```bash

# Compile server
gcc -o server server.c common.c -lpthread


# Compile client (requires ncurses)
gcc -o client client.c common.c -lpthread -lncurses
```

## How to Start:
```bash

# Run server
./server

# Run client
./client 127.0.0.1 NomeDoCliente
```


## How to Terminate
```bash

ps aux | grep "server"

sudo kill -9 <Server_PID>

ps aux | grep "client"

sudo kill -9 <Client_PID>
```
