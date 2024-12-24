#define REUSE 1
#define REVDNS 1
#define MAX_CLIENTS 10
#define MAX_GAMES 10
#define MAX_BUFFER_SIZE 1024
#define PASSWORD "ok"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include <stdbool.h>
#include <sqlite3.h>

#ifdef REVDNS
#include <netdb.h>
#endif

typedef struct Client {
    int socket_fd;
    struct sockaddr_in addr;
    int id;
    char name[50];
    int is_connected;
    int score;
    int is_authenticated;
    int wins;
    int losses;
    int forfeit;
    int games_played;
    int current_game_id;
    int received;
}Client;

typedef struct Game {
    int id;
    Client *player1;
    Client *player2;
    int is_finished;
    int board[19][19];
    int turn;
    int last_move_row;
    int last_move_col;
    int last_player_turn;
    int winner;
    int player1_captures;
    int player2_captures;
} Game;

void sendPacket(const char* buffer, Client client);

char* processcmd(char *buffer, Client *client, Game *available_games, Game *active_games, int *curr_available_games, int *curr_active_games, sqlite3 *db);

void init_clients(Client clients[]);

void closeconnection(Client *client);

bool createGame(Client *client, Game *available_games, int *curr_available_games);

void formatCommand(char *buffer);

bool joinGame(int game_id, Client *client, Game *available_games, int *curr_available_games, Game *active_games, int *curr_active_games);

void initializePlayer(Client *player);

void displayGameList(int curr_available_games, const Game *available_games, char *games_list);

bool removeGame(int game_id, Game *games, int *curr_games);

void finishGame(Game *game, Client *winner);

void handle_forfeit(Game *game, Client *forfeiter, int *curr_active_games, Game *active_games);

bool quit_game(Game *games, int *curr_available_games, const Client *client);

bool check_move(int row, int col, Game *game);

char* get_board(Game *game);

bool check_win(Game *game, int row, int col, int turn);

void capturePieces(Game *game, int row, int col, int turn);

bool openDBConnection(sqlite3 **db);

bool createTableClients(sqlite3 **db);

bool insertClient(sqlite3 *db, char *username, char *password);

bool getClient(sqlite3 *db, char *name, char *password, Client *client);

bool deleteClient(int id, sqlite3 *db);

bool updateClient(sqlite3 *db, Client client);

unsigned char *hash_password(const char *password, unsigned int *out_len);

void print_hash(const unsigned char *hash, unsigned int len);

char* handleConnectCommand(Client *client, char *buffer, Game *available_games, const int *curr_available_games, sqlite3 *db);

void handleDisconnect(Client *client, char *response);

void handleStatusCommand(Client *client, char *response, const Game *available_games, const int *curr_available_games, Game *active_games, int *curr_active_games, sqlite3 *db);

char* handleJoinCommand(char *response, Client *client, Game *available_games, int *curr_available_games, Game *active_games, int *curr_active_games);

bool isGameFull(int game_id, const Game *available_games, const int *curr_available_games, const Game *active_games, const int *curr_active_games);

bool handleForfeit(Client *client, Game *active_games, int *curr_active_games, char *response);

bool createAccount(sqlite3 *db, char *buffer, char *response);

char* handleMoveCommand(Client *client, char *response, Game *active_games, const int *curr_active_games);

int main() {
    int s, clilen, flags, max_sd, sd, activity, new_s, valread, i;
    Client clients[MAX_CLIENTS];
    Game available_games[MAX_GAMES];
    int curr_available_games = 0;
    Game active_games[MAX_GAMES];
    int curr_active_games = 0;
    struct sockaddr_in srv, cli;
    fd_set readfds;
    char buffer[513];
    sqlite3 *db;
    char *response = 0;
#ifdef REUSE
    int optval;
#endif
    if (!openDBConnection(&db)) {
        return 1;
    }
    init_clients(clients);

    // Créer le socket du serveur
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == 0) {
        perror("Erreur lors de la création du socket");
        return 1;
    }

    // Réutiliser le même socket si l'application est fermée
#ifdef REUSE
    optval = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));
#endif

    // Mode non bloquant
    flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    // Lier le socket à une adresse
    bzero(&srv, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    srv.sin_port = htons(55555);
    if (bind(s, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        perror("Erreur de bind");
        return 1;
    }

    // Mettre le socket en mode écoute
    if (listen(s, 3) != 0) {
        perror("Erreur de listen");
        return 1;
    }

    printf("Serveur en écoute sur le port 55555\n");

    while (1) {
        clilen = sizeof(cli);
        // Effacer et préparer les descripteurs
        FD_ZERO(&readfds);

        // Ajouter le socket du serveur
        FD_SET(s, &readfds);
        max_sd = s;

        // Ajouter les sockets clients actifs au set
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].socket_fd;

            if (sd > 0) {
                FD_SET(sd, &readfds);
            }

            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        // Attendre une activité sur un des sockets
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            printf("Erreur avec select\n");
            break;
        }

        // Si une nouvelle connexion arrive
        if (FD_ISSET(s, &readfds)) {
            new_s = accept(s, (struct sockaddr *)&cli, (socklen_t*)&clilen);
            if (new_s < 0) {
                perror("Erreur d'acceptation");
                break;
            }

            printf("Nouvelle connexion : socket fd est %d, IP est : %s, Port : %d\n",
                   new_s, inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

            // Ajouter le nouveau client à la liste
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].socket_fd == 0) {
                    clients[i].socket_fd = new_s;
                    clients[i].addr = cli;
                    printf("Ajouté à la liste des sockets à l'index %d\n", i);
                    break;
                }
            }
        }

        // Vérifier l'activité sur les sockets clients
        for (i = 0; i < MAX_CLIENTS; i++) {
            sd = clients[i].socket_fd;
            if(sd == 0) {
                continue;
            }

            if (FD_ISSET(sd, &readfds)) {
                // Lire le message
                if ((valread = (int)read(sd, buffer, 512)) == 0) {
                    // Le client a fermé la connexion
                    getpeername(sd, (struct sockaddr *)&cli, (socklen_t*)&clilen);
                    printf("Client déconnecté : IP %s, Port %d\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

                    // Fermer le socket et le marquer comme disponible
                    close(sd);
                    closeconnection(&clients[i]);
                } else {
                    buffer[valread] = '\0';
                    printf("Réception de la réponse avec read() : %s\n", buffer);
                    // Envoyer le message au client
                    response = processcmd(buffer, &clients[i], available_games, active_games, &curr_available_games, &curr_active_games, db);

                    if (response != NULL && response != "DISCONNECTED") {
                        printf("Commande envoyée : %s\n", response);
                        sendPacket(response, clients[i]);
                    }

                    free(response);
                }
            }
        }
    }

    sqlite3_close(db);
    return 0;
}

char* processcmd(char *buffer, Client *client, Game *available_games, Game *active_games, int *curr_available_games, int *curr_active_games, sqlite3 *db) {
    char *response = (char*)malloc(MAX_BUFFER_SIZE);

    if(!response) {
        perror("Allocation de la réponse a échoué.");
        return NULL;
    }

    char* verb = strtok(buffer, " ");

    if(verb == NULL) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Commande invalide : %s", verb);
        return response;
    }

    formatCommand(verb);

    if(strcmp(verb, "CONNECT") == 0) {
        char *result = handleConnectCommand(client, buffer, available_games, curr_available_games, db);
        free(response);
        return result;
    } else if(strcmp(verb, "DISCONNECT") == 0) {
        handleDisconnect(client, response);
    } else if (strcmp(verb, "MOVE") == 0) {
        handleMoveCommand(client, response, active_games, curr_active_games);
    } else if(strcmp(verb, "CREATE") == 0) {
        if(!createGame(client, available_games, curr_available_games)) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Impossible de créer une partie");
            return response;
        }
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "CREATEOK");
    } else if(strcmp(verb, "JOIN") == 0) {
        char *result = handleJoinCommand(response, client, available_games, curr_available_games, active_games, curr_active_games);
        free(response);
        return result;
    } else if(strcmp(verb, "STATS") == 0) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Stats : Name : %s, Wins : %d, Losses : %d", client->name, client->wins, client->losses);
    } else if(strcmp(verb, "QUIT") == 0) {
        if (!quit_game(available_games, curr_available_games, client)) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Impossible de quitter la partie");
        } else {
            char games_list[MAX_BUFFER_SIZE];
            displayGameList(*curr_available_games, available_games, games_list);
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "QUITOK %s", games_list);
        }
    } else if (strcmp(verb, "LIST") == 0){
        char games_list[MAX_BUFFER_SIZE];
        displayGameList(*curr_available_games, available_games, games_list);
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "LISTOK %s", games_list);
    } else if (strcmp(verb, "FORFEIT") == 0) {
        if (!handleForfeit(client, active_games, curr_active_games, response)) {
            return response;
        }
    } else if (strcmp(verb, "ISFULL") == 0) {
        int game_id = client->current_game_id;
        bool is_full = isGameFull(game_id, available_games, curr_available_games, active_games, curr_active_games);
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, is_full ? "YES" : "NO");
    } else if (strcmp(verb, "STATUS") == 0) {
        handleStatusCommand(client, response, available_games, curr_available_games, active_games, curr_active_games, db);
    } else if (strcmp(verb, "CREATEACCOUNT") == 0) {
        if (!createAccount(db, buffer, response)) {
            return response;
        }
    } else {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Commande invalide : %s + length : %lu", verb, strlen(verb));
    }

    return response;
}

void sendPacket(const char* buffer, Client client) {
    if(client.socket_fd > 0) {
        if (send(client.socket_fd, buffer, strlen(buffer), 0) == -1) {
            if (errno == EPIPE) {
                printf("Erreur: Broken pipe\n");
                closeconnection(&client);
            } else {
                perror("Erreur lors de l'envoi du paquet");
            }
        }
        return;
    }

    printf("Socket client invalide");
}

void init_clients(Client clients[]) {
    for(int i = 0;i<MAX_CLIENTS;i++) {
        clients[i].socket_fd = 0;
        clients[i].is_connected = 0;
        clients[i].id = 0;
        clients[i].name[0] = '\0';
        clients[i].wins = 0;
        clients[i].losses = 0;
        clients[i].games_played = 0;
        clients[i].current_game_id = -1;
        clients[i].is_authenticated = 0;
        clients[i].score = 0;
        clients[i].forfeit = 0;
        clients[i].received = 0;
    }
}

void closeconnection(Client *client) {
    close(client->socket_fd);
    client->id = 0;
    client->socket_fd = 0;
    client->is_connected = 0;
    client->name[0] = '\0';
    client->wins = 0;
    client->losses = 0;
    client->games_played = 0;
    client->current_game_id = -1;
    client->is_authenticated = 0;
    client->score = 0;
    client->forfeit = 0;
}

bool createGame(Client *client, Game *available_games, int *curr_available_games) {
    Game new_game;

    if(*curr_available_games >= MAX_GAMES) {
        return false;
    }

    if(client->is_connected == 0) {
        printf("Client not connected\n");
        return false;
    }

    if(client->current_game_id != -1) {
        printf("Client already in a game\n");
        return false;
    }

    new_game.id = *curr_available_games;
    new_game.player1 = client;
    new_game.last_move_row = -1;
    new_game.last_move_col = -1;
    new_game.turn = 2; // Par défaut, le joueur 1 commence
    new_game.last_player_turn = 2;
    new_game.winner = 0;
    new_game.player2 = NULL;
    new_game.player1_captures = 0;
    new_game.player2_captures = 0;
    for (int i = 0; i < 19; i++) {
        for (int j = 0; j < 19; j++) {
            new_game.board[i][j] =  0;;
        }
    }
    new_game.board[9][9] = 1;
    new_game.is_finished = 0;

    client->current_game_id = new_game.id;

    available_games[*curr_available_games] = new_game;
    (*curr_available_games)++;
    return true;
}

void formatCommand(char *buffer) {
    size_t len = strlen(buffer);
    if (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[len - 1] = '\0';
    }
    if (len > 1 && (buffer[len - 2] == '\n' || buffer[len - 2] == '\r')) {
        buffer[len - 2] = '\0';
    }
}

bool joinGame(int game_id, Client *client, Game *available_games, int *curr_available_games, Game *active_games, int *curr_active_games) {
    if(client->current_game_id != -1) {
        printf("Client already in a game\n");
        return false;
    }

    for(int i = 0;i<*curr_available_games;i++) {
        if(available_games[i].id == game_id) {
            if(available_games[i].player2 != NULL && available_games[i].player2->socket_fd != 0) {
                printf("Game is full\n");
                return false;
            }

            active_games[*curr_active_games] = available_games[i];
            (*curr_active_games)++;

            active_games[*curr_active_games-1].player2 = client;
            client->current_game_id = *curr_active_games-1;

            for (int j = i; j < *curr_available_games - 1; j++) {
                available_games[j] = available_games[j + 1];
            }
            (*curr_available_games)--;

            return true;
        }
    }

    printf("Game not found\n");
    return false;
}

void initializePlayer(Client *player) {
    player->socket_fd = 0;
    player->is_connected = 0;
    player->id = 0;
    player->name[0] = '\0';
    player->wins = 0;
    player->losses = 0;
    player->games_played = 0;
    player->current_game_id = -1;
    player->is_authenticated = 0;
    player->score = 0;
    player->forfeit = 0;
    player->received = 0;
}

void displayGameList(int curr_available_games, const Game *available_games, char *games_list) {
    games_list[0] = '\0';  // Initialise la chaîne
    for (int i = 0; i < curr_available_games; i++) {
        char game_id_str[MAX_BUFFER_SIZE];
        snprintf(game_id_str, MAX_BUFFER_SIZE, "%d;%s;%d;%d;%d;%d",
                 available_games[i].id, available_games[i].player1->name,
                 available_games[i].player1->score, available_games[i].player1->wins,
                 available_games[i].player1->losses, available_games[i].player1->forfeit);

        // Vérifie que le buffer peut accueillir la nouvelle partie
        if (strlen(games_list) + strlen(game_id_str) + 1 < MAX_BUFFER_SIZE) {
            if (strlen(games_list) > 0) {
                strcat(games_list, ",");
            }
            strcat(games_list, game_id_str);
        } else {
            printf("Erreur : dépassement de buffer dans displayGameList\n");
            break;
        }
    }

    if (strlen(games_list) == 0) {
        strcat(games_list, "NONE");
    }
}

bool removeGame(int game_id, Game *games, int *curr_games) {
    for(int i = 0;i<*curr_games;i++) {
        if(games[i].id == game_id) {
            for(int j = i;j<*curr_games-1;j++) {
                games[j] = games[j+1];
            }

            (*curr_games)--;
            return true;
        }
    }

    return false;
}

void finishGame(Game *game, Client *winner) {
    game->is_finished = 1;
    game->winner = (winner == game->player1) ? 1 : 2;

    if (winner == game->player1) {
        game->player1->wins++;
        game->player2->losses++;
    } else {
        game->player2->wins++;
        game->player1->losses++;
    }

    game->player1->games_played++;
    game->player2->games_played++;
}

void handle_forfeit(Game *game, Client *forfeiter, int *curr_active_games, Game *active_games) {
    if (game->player1 == forfeiter) {
        game->player2->wins++;
        game->player1->forfeit++;
        game->player1->score -= 100;
        game->player2->score += 100;
    } else {
        game->player1->wins++;
        game->player2->forfeit++;
        game->player2->score -= 100;
        game->player1->score += 100;
    }

    game->winner = (game->player1 == forfeiter) ? 2 : 1;
    game->player1->games_played++;
    game->player2->games_played++;
    game->is_finished = 1;
}

bool quit_game(Game *games, int *curr_available_games, const Client *client) {
    for (int i = 0; i < *curr_available_games; i++) {
        if (games[i].player1 == client) {
            games[i].player1->current_game_id = -1;
            if (games[i].player2 != NULL) {
                games[i].player2->current_game_id = -1;
            }
            (*curr_available_games)--;

            if (removeGame(i, games, curr_available_games)) {
                return true;
            }
        }
    }

    return false;
}

bool check_move(int row, int col, Game *game) {
    if (row < 0 || row >= 19 || col < 0 || col >= 19) {
        return false;
    }

    if (game->board[row][col] != 0) {
        return false;
    }

    return true;
}

char* get_board(Game *game) {
    char *buffer = (char*)malloc(361 + 18 * 19 + 1); // 361 characters + 18 commas per row + 1 for null terminator
    int index = 0;
    for (int i = 0; i < 19; i++) {
        for (int j = 0; j < 19; j++) {
            buffer[index++] = game->board[i][j] + '0';
            if (j < 18) {
                buffer[index++] = ',';
            }
        }
        if (i < 18) {
            buffer[index++] = ',';
        }
    }
    buffer[index] = '\0';
    return buffer;
}

bool check_win(Game *game, int row, int col, int turn) {
    if (turn == 1 && game->player1_captures == 10) {
        return true;
    }

    if (turn == 2 && game->player2_captures == 10) {
        return true;
    }

    int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    int player = turn;

    for (int d = 0; d < 4; d++) {
        int count = 1;
        for (int i = 1; i < 5; i++) {
            int new_row = row + i * directions[d][0];
            int new_col = col + i * directions[d][1];
            if (new_row >= 0 && new_row < 19 && new_col >= 0 && new_col < 19 && game->board[new_row][new_col] == player) {
                count++;
            } else {
                break;
            }
        }
        for (int i = 1; i < 5; i++) {
            int new_row = row - i * directions[d][0];
            int new_col = col - i * directions[d][1];
            if (new_row >= 0 && new_row < 19 && new_col >= 0 && new_col < 19 && game->board[new_row][new_col] == player) {
                count++;
            } else {
                break;
            }
        }
        if (count >= 5) {
            return true;
        }
    }
    return false;
}

void capturePieces(Game *game, int row, int col, int turn) {
    int directions[8][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}, {0, -1}, {-1, 0}, {-1, -1}, {-1, 1}};
    int opponent = (turn == 1) ? 2 : 1;

    printf("LOG: Starting capture check for player %d at (%d, %d)\n", turn, row, col);
    printf("LOG: Initial board state at (%d, %d): %d\n", row, col, game->board[row][col]);

    for (int d = 0; d < 8; d++) {
        int mid1_row = row + directions[d][0];
        int mid1_col = col + directions[d][1];
        int mid2_row = row + 2 * directions[d][0];
        int mid2_col = col + 2 * directions[d][1];
        int end_row = row + 3 * directions[d][0];
        int end_col = col + 3 * directions[d][1];

        printf("LOG: Checking direction %d: (%d, %d), (%d, %d), (%d, %d)\n", d, mid1_row, mid1_col, mid2_row, mid2_col, end_row, end_col);
        printf("LOG: Values: mid1 = %d, mid2 = %d, end = %d\n",
               game->board[mid1_row][mid1_col],
               game->board[mid2_row][mid2_col],
               game->board[end_row][end_col]);

        if (mid1_row >= 0 && mid1_row < 19 && mid1_col >= 0 && mid1_col < 19 &&
            mid2_row >= 0 && mid2_row < 19 && mid2_col >= 0 && mid2_col < 19 &&
            end_row >= 0 && end_row < 19 && end_col >= 0 && end_col < 19 &&
            game->board[mid1_row][mid1_col] == opponent &&
            game->board[mid2_row][mid2_col] == opponent &&
            game->board[end_row][end_col] == turn) {

            printf("LOG: Capture detected at: (%d, %d) and (%d, %d)\n", mid1_row, mid1_col, mid2_row, mid2_col);

            game->board[mid1_row][mid1_col] = 0;
            game->board[mid2_row][mid2_col] = 0;

            if (turn == 1) {
                game->player1_captures += 2;
                game->player1->score += 2;
            } else {
                game->player2_captures += 2;
                game->player2->score += 2;
            }
        }
    }
    printf("LOG: Finished capture check for player %d. Captures: Player 1: %d, Player 2: %d\n",
           turn, game->player1_captures, game->player2_captures);
}

bool openDBConnection(sqlite3 **db) {
    int rc = sqlite3_open("ProjetReseaux.db", db);  // "example.db" sera créée si elle n'existe pas.

    if (rc) {
        printf("Can't open database: %s\n", sqlite3_errmsg(*db));
        return false;
    }

    printf("Opened database successfully\n");
    return true;
}

bool createTableClients(sqlite3 **db) {
    const char *sql = "CREATE TABLE clients ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT NOT NULL, "
                  "password BLOB NOT NULL, "
                  "wins INTEGER DEFAULT 0, "
                  "losses INTEGER DEFAULT 0, "
                  "forfeit INTEGER DEFAULT 0, "
                  "games_played INTEGER DEFAULT 0, "
                  "score INTEGER DEFAULT 0"
                  ");";


    char *err_msg = 0;

    int rc = sqlite3_exec(*db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        printf("Erreur lors de la création de la table : %s\n", err_msg);
        sqlite3_free(err_msg);
        return false;
    }

    printf("Table créée avec succès\n");

    return true;
}

bool insertClient(sqlite3 *db, char *username, char *password) {
    const char *sql = "INSERT INTO clients (name, password, wins, losses, forfeit, games_played, score) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt;

    // Préparer la requête SQL
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        printf("Erreur lors de la préparation de la requête : %s\n", sqlite3_errmsg(db));
        return false;
    }

    // Hacher le mot de passe
    unsigned int hash_len;
    unsigned char *hashed_password = hash_password(password, &hash_len);
    if (hashed_password == NULL) {
        sqlite3_finalize(stmt);
        return false;
    }

    // Lier les valeurs à la requête SQL
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);  // Nom
    sqlite3_bind_blob(stmt, 2, hashed_password, hash_len, SQLITE_STATIC);  // Mot de passe haché en tant que BLOB
    sqlite3_bind_int(stmt, 3, 0);  // wins
    sqlite3_bind_int(stmt, 4, 0);  // losses
    sqlite3_bind_int(stmt, 5, 0);  // forfeit
    sqlite3_bind_int(stmt, 6, 0);  // games_played
    sqlite3_bind_int(stmt, 7, 0);  // score

    // Exécuter la requête SQL
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("Erreur lors de l'exécution de la requête : %s\n", sqlite3_errmsg(db));
        free(hashed_password);
        sqlite3_finalize(stmt);
        return false;
    }

    printf("Données insérées avec succès\n");

    // Libérer la mémoire
    free(hashed_password);
    sqlite3_finalize(stmt);
    return true;
}

bool getClient(sqlite3 *db, char *name, char *password, Client *client) {
    const char *sql = "SELECT * FROM clients WHERE name = ? and password = ?;";
    sqlite3_stmt *stmt;
    unsigned int hash_len;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        printf("Erreur lors de la préparation de la requête : %s\n", sqlite3_errmsg(db));
        return false;
    }

    unsigned char *has = hash_password(password, &hash_len);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, has, hash_len, SQLITE_STATIC);

    printf("getClient : \n");
    print_hash(has, hash_len);
    bool user_found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *found_name = (const char *)sqlite3_column_text(stmt, 1);
        const unsigned char *found_password = sqlite3_column_blob(stmt, 2);
        int wins = sqlite3_column_int(stmt, 3);
        int losses = sqlite3_column_int(stmt, 4);
        int forfeit = sqlite3_column_int(stmt, 5);
        int games_played = sqlite3_column_int(stmt, 6);
        int score = sqlite3_column_int(stmt, 7);

        printf("User found: ID = %d, Name = %s, Password = %p, Wins = %d, Losses = %d, Forfeit = %d, Games Played = %d, Score = %d\n",
               id, found_name, found_password, wins, losses, forfeit, games_played, score);
        print_hash(found_password, hash_len);
        user_found = true;
        client->id = id;
        strcpy(client->name, found_name);
        client->wins = wins;
        client->losses = losses;
        client->forfeit = forfeit;
        client->games_played = games_played;
        client->score = score;
    }

    free(has);
    sqlite3_finalize(stmt);
    return user_found;
}

bool deleteClient(int id, sqlite3 *db) {
    const char *sql = "DELETE FROM clients WHERE id = ?;";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        printf("Erreur lors de la préparation de la requête : %s\n", sqlite3_errmsg(db));
        return false;
    }

    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("Erreur lors de l'exécution de la requête : %s\n", sqlite3_errmsg(db));
        return false;
    }

    printf("Data deleted successfully\n");
    sqlite3_finalize(stmt);
    return true;
}

bool updateClient(sqlite3 *db, Client client) {
    const char *sql = "UPDATE clients SET wins = ?, losses = ?, forfeit = ?, games_played = ?, score = ? WHERE id = ?;";
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        printf("Erreur lors de la préparation de la requête : %s\n", sqlite3_errmsg(db));
        return false;
    }
    sqlite3_bind_int(stmt, 1, client.wins);
    sqlite3_bind_int(stmt, 2, client.losses);
    sqlite3_bind_int(stmt, 3, client.forfeit);
    sqlite3_bind_int(stmt, 4, client.games_played);
    sqlite3_bind_int(stmt, 5, client.score);
    sqlite3_bind_int(stmt, 6, client.id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("Erreur lors de l'exécution de la requête : %s\n", sqlite3_errmsg(db));
        return false;
    }

    printf("Data updated successfully\n");
    sqlite3_finalize(stmt);
    return true;
}

unsigned char *hash_password(const char *password, unsigned int *out_len) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;

    // Créer un contexte pour l'algorithme de hachage
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        fprintf(stderr, "Error creating digest context\n");
        return NULL;
    }

    // Initialiser le contexte pour utiliser l'algorithme SHA-256 (vous pouvez utiliser MD5 ou SHA-1 si vous préférez)
    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1) {
        fprintf(stderr, "Error initializing digest\n");
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    // Mettre à jour le contexte avec le mot de passe
    if (EVP_DigestUpdate(mdctx, password, strlen(password)) != 1) {
        fprintf(stderr, "Error updating digest\n");
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    // Finaliser le hachage
    if (EVP_DigestFinal_ex(mdctx, digest, &digest_len) != 1) {
        fprintf(stderr, "Error finalizing digest\n");
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    // Libérer le contexte du hachage
    EVP_MD_CTX_free(mdctx);

    // Allouer de la mémoire pour le tableau binaire
    unsigned char *hash_binary = malloc(digest_len);
    if (hash_binary == NULL) {
        fprintf(stderr, "Error allocating memory for hash\n");
        return NULL;
    }

    // Copier les octets du hachage dans le tableau binaire
    memcpy(hash_binary, digest, digest_len);

    // Retourner le tableau binaire et sa longueur
    *out_len = digest_len;

    return hash_binary;
}

void print_hash(const unsigned char *hash, unsigned int len) {
    for (unsigned int i = 0; i < len; i++) {
        printf("%02x", hash[i]);  // Affichage de chaque octet en hexadécimal
    }
    printf("\n");
}

char* handleConnectCommand(Client *client, char *buffer, Game *available_games, const int *curr_available_games, sqlite3 *db) {
    char *response = (char*)malloc(MAX_BUFFER_SIZE);

    if (!response) {
        perror("Allocation de la réponse a échoué.");
        return NULL;
    }

    char *player_name = strtok(NULL, " ");
    if (client->is_connected == 1) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Vous êtes déjà connecté");
        return response;
    }

    if (player_name == NULL || strcmp(player_name, "") == 0) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Pseudonyme invalide");
        return response;
    }

    char *password = strtok(NULL, " ");
    if (password == NULL) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Mot de passe invalide : %s + %d", password, strcmp(password, PASSWORD));
        return response;
    }

    if (!getClient(db, player_name, password, client)) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Erreur d'authentification");
        return response;
    }

    client->is_connected = 1;

    char games_list[MAX_BUFFER_SIZE];
    displayGameList(*curr_available_games, available_games, games_list);

    memset(response, 0, MAX_BUFFER_SIZE);
    snprintf(response, MAX_BUFFER_SIZE, "CONNECTOK %s", games_list);
    return response;
}

void handleDisconnect(Client *client, char *response) {
    printf("LOG: DISCONNECT command received from %s\n", client->name);

    // Vérifiez si le socket est valide avant d'envoyer le message
    if (client->socket_fd > 0) {
        // Répondre avec un message de confirmation
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "DISCONNECTED");
        sendPacket(response, *client);
    }

    // Fermer la connexion du client
    closeconnection(client);
}

void handleStatusCommand(Client *client, char *response, const Game *available_games, const int *curr_available_games, Game *active_games, int *curr_active_games, sqlite3 *db) {
    printf("LOG: STATUS command received from %s\n", client->name);

    if (client->current_game_id == -1) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "ERROR: No active game");
        return;
    }

    char games_list[MAX_BUFFER_SIZE];
    displayGameList(*curr_available_games, available_games, games_list);

    for (int i = 0; i < *curr_active_games; i++) {
        if (active_games[i].id == client->current_game_id) {
            Game *game = &active_games[i];
            if (game->is_finished) {
                if ((game->winner == 1 && game->player1 == client) || (game->winner == 2 && game->player2 == client)) {
                    client->current_game_id = -1;
                    memset(response, 0, MAX_BUFFER_SIZE);
                    snprintf(response, MAX_BUFFER_SIZE, "WINNER %s", games_list);
                    updateClient(db, *client);
                    client->received = 1;
                    printf("Winner victory : %d\n", client->wins);
                } else {
                    memset(response, 0, MAX_BUFFER_SIZE);
                    snprintf(response, MAX_BUFFER_SIZE, "LOSER %s", games_list);
                    updateClient(db, *client);
                    client->current_game_id = -1;
                    client->received = 1;
                    printf("Loser losses : %d\n", client->forfeit);
                }
                if (game->player1->received == 1 && game->player2->received == 1) {
                    removeGame(game->id, active_games, curr_active_games);
                    game->player1->received = 0;
                    game->player2->received = 0;
                }
            } else {
                // Handle ongoing game status
                if ((game->last_player_turn == 1 && game->player2 == client) ||
                    (game->last_player_turn == 2 && game->player1 == client)) {
                    if (game->last_move_row != -1 && game->last_move_col != -1) {
                        memset(response, 0, MAX_BUFFER_SIZE);
                        char *board = get_board(game);
                        snprintf(response, MAX_BUFFER_SIZE, "MOVE %s", board);
                        game->last_move_row = -1;
                        game->last_move_col = -1;
                        free(board);
                    } else {
                        memset(response, 0, MAX_BUFFER_SIZE);
                        snprintf(response, MAX_BUFFER_SIZE, "NOTENDED");
                    }
                } else {
                    memset(response, 0, MAX_BUFFER_SIZE);
                    snprintf(response, MAX_BUFFER_SIZE, "NOTENDED");
                }
            }
            return;
        }
    }

    memset(response, 0, MAX_BUFFER_SIZE);
    snprintf(response, MAX_BUFFER_SIZE, "ENDED %s", games_list);
    client->current_game_id = -1;
}

char* handleJoinCommand(char *response, Client *client, Game *available_games, int *curr_available_games, Game *active_games, int *curr_active_games) {
    char *game_id_string = strtok(NULL, " ");
    if(game_id_string == NULL) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Identifiant de partie invalide");
        return response;
    }

    int game_id = atoi(game_id_string);
    if(game_id >= *curr_available_games) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Identifiant de partie invalide");
        return response;
    }

    if(!joinGame(game_id, client, available_games, curr_available_games, active_games, curr_active_games)) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Impossible de rejoindre la partie.");
        return response;
    }

    memset(response, 0, MAX_BUFFER_SIZE);
    snprintf(response, MAX_BUFFER_SIZE, "JOINOK");
    return response;
}

bool isGameFull(int game_id, const Game *available_games, const int *curr_available_games, const Game *active_games, const int *curr_active_games) {
    bool is_full = false;

    // Vérifie dans available_games
    for (int i = 0; i < *curr_available_games; i++) {
        if (available_games[i].id == game_id) {
            // La partie est dans available_games : vérifie si elle est complète
            if (available_games[i].player2 != NULL && available_games[i].player2->socket_fd != 0) {
                is_full = true;  // Complète
            }
            break;  // Partie trouvée, inutile de continuer
        }
    }

    // Vérifie dans active_games si pas encore trouvée
    if (!is_full) {
        for (int i = 0; i < *curr_active_games; i++) {
            if (active_games[i].id == game_id) {
                is_full = true;  // Une partie dans active_games est toujours complète
                break;  // Partie trouvée
            }
        }
    }

    return is_full;
}

bool handleForfeit(Client *client, Game *active_games, int *curr_active_games, char *response) {
    if (client->current_game_id == -1) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Vous n'êtes pas dans une partie");
        return false;
    }

    for (int i = 0; i < *curr_active_games; i++) {
        if (active_games[i].id == client->current_game_id) {
            handle_forfeit(&active_games[i], client, curr_active_games, active_games);
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "FORFEITOK");
            return true;
        }
    }

    memset(response, 0, MAX_BUFFER_SIZE);
    snprintf(response, MAX_BUFFER_SIZE, "Impossible de trouver la partie");
    return false;
}

bool createAccount(sqlite3 *db, char *buffer, char *response) {
    char *player_name = strtok(NULL, " ");
    if (player_name == NULL || strcmp(player_name, "") == 0) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Pseudonyme invalide");
        return false;
    }

    char *password = strtok(NULL, " ");
    if (password == NULL || strcmp(password, "") == 0) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Mot de passe invalide");
        return false;
    }

    if (!insertClient(db, player_name, password)) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Erreur lors de la création du compte");
        return false;
    }
    memset(response, 0, MAX_BUFFER_SIZE);
    snprintf(response, MAX_BUFFER_SIZE, "CREATEACCOUNTOK");
    return true;
}

char* handleMoveCommand(Client *client, char *response, Game *active_games, const int *curr_active_games) {
    const int row = atoi(strtok(NULL, " "));
    const int col = atoi(strtok(NULL, " "));
    printf("LOG: MOVE command received from %s for (%d, %d)\n", client->name, row, col);

    for (int i = 0; i < *curr_active_games; i++) {
        if (active_games[i].id == client->current_game_id) {
            Game *game = &active_games[i];

            if ((game->turn == 1 && game->player1 == client) || (game->turn == 2 && game->player2 == client)) {
                if (check_move(row, col, game)) {
                    printf("LOG: Valid move from %s\n", client->name);
                    game->board[row][col] = game->turn;
                    capturePieces(game, row, col, game->turn);

                    if (check_win(game, row, col, game->turn)) {
                        printf("LOG: %s won the game\n", client->name);
                        finishGame(game, client);
                    }

                    // Mettre à jour le plateau et enregistrer le mouvement
                    game->last_move_row = row;
                    game->last_move_col = col;
                    game->last_player_turn = (game->turn == 1) ? 1 : 2; // Enregistre le joueur actif

                    // Passe au tour suivant
                    game->turn = (game->turn == 1) ? 2 : 1;

                    memset(response, 0, MAX_BUFFER_SIZE);
                    char *board = get_board(game);
                    snprintf(response, MAX_BUFFER_SIZE, "MOVEOK %s", board);
                    free(board);
                } else {
                    printf("LOG: Invalid move attempted by %s\n", client->name);
                    memset(response, 0, MAX_BUFFER_SIZE);
                    snprintf(response, MAX_BUFFER_SIZE, "ERROR: Invalid move");
                }
            } else {
                printf("LOG: %s tried to play out of turn\n", client->name);
                memset(response, 0, MAX_BUFFER_SIZE);
                snprintf(response, MAX_BUFFER_SIZE, "ERROR: Not your turn");
            }
            return response;
        }
    }
    printf("LOG: No active game found for client %s\n", client->name);
    memset(response, 0, MAX_BUFFER_SIZE);
    snprintf(response, MAX_BUFFER_SIZE, "ERROR: No active game");
    return response;
}