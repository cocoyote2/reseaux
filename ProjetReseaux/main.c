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
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>

#ifdef REVDNS
#include <netdb.h>
#endif

typedef struct Client {
    int socket_fd;
    struct sockaddr_in addr;
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

    int turn;  // 1 pour le joueur 1, 2 pour le joueur 2
    int last_move_row;  // Dernière ligne jouée
    int last_move_col;  // Dernière colonne jouée
    int last_player_turn;
    int winner;
    int player1_captures;
    int player2_captures;
} Game;

void sendPacket(const char* buffer, Client client);

char* processcmd(char *buffer, Client *client, Game *available_games, Game *active_games, int *curr_available_games, int *curr_active_games);

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

void test_capturePieces();

int both_received = 0;

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
    srand(time(NULL));

#ifdef REUSE
    int optval;
#endif

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
        }

        // Si une nouvelle connexion arrive
        if (FD_ISSET(s, &readfds)) {
            new_s = accept(s, (struct sockaddr *)&cli, (socklen_t*)&clilen);
            if (new_s < 0) {
                perror("Erreur d'acceptation");
                return 1;
            }

            printf("Nouvelle connexion : socket fd est %d, IP est : %s, Port : %d\n",
                   new_s, inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));

            // Ajouter le nouveau client à la liste
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].socket_fd == 0) {
                    clients[i].socket_fd = new_s;
                    //clients[i].is_connected = 1;
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
                    char *response = processcmd(buffer, &clients[i], available_games, active_games, &curr_available_games, &curr_active_games);

                    if (response != NULL && response != "DISCONNECTED") {
                        printf("Commande envoyée : %s\n", response);
                        sendPacket(response, clients[i]);
                    }

                    free(response);
                }
            }
        }
    }
}

char* processcmd(char *buffer, Client *client, Game *available_games, Game *active_games, int *curr_available_games, int *curr_active_games) {
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
    }else {
        formatCommand(verb);
    }

    if(strcmp(verb, "CONNECT") == 0) {
        char *player_name = strtok(NULL, " ");
        if(client->is_connected == 1) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Vous êtes déjà connecté");
            return response;
        }

        if(player_name == NULL || strcmp(player_name, "") == 0) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Pseudonyme invalide");
            return response;
        }

        char *password = strtok(NULL, " ");

        if (password != NULL) {
            formatCommand(password);
        }

        if(password == NULL || strcmp(password, PASSWORD) != 0) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Mot de passe invalide : %s + %d", password, strcmp(password, PASSWORD));
            return response;
        }

        strcpy(client->name, player_name);
        client->is_connected = 1;

        char games_list[MAX_BUFFER_SIZE];
        displayGameList(*curr_available_games, available_games, games_list);

        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "OK %s", games_list);
    }else if(strcmp(verb, "DISCONNECT") == 0) {
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
    }else if (strcmp(verb, "MOVE") == 0) {
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
    }else if(strcmp(verb, "CREATE") == 0) {
        if(!createGame(client, available_games, curr_available_games)) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Impossible de créer une partie");
            return response;
        }

        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "CREATEOK");
    }else if(strcmp(verb, "JOIN") == 0) {
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
    }else if(strcmp(verb, "STATS") == 0) {
        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Stats : Name : %s, Wins : %d, Losses : %d", client->name, client->wins, client->losses);
    }else if(strcmp(verb, "QUIT") == 0) {
        if (!quit_game(available_games, curr_available_games, client)) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Impossible de quitter la partie");
        }

        char games_list[MAX_BUFFER_SIZE];

        displayGameList(*curr_available_games, available_games, games_list);

        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "QUITOK %s", games_list);
    }else if (strcmp(verb, "LIST") == 0){
        char games_list[MAX_BUFFER_SIZE];
        displayGameList(*curr_available_games, available_games, games_list);

        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "LISTOK %s", games_list);
    }else if (strcmp(verb, "FORFEIT") == 0) {
        if (client->current_game_id == -1) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "Vous n'êtes pas dans une partie");
            return response;
        }

        for (int i = 0; i < *curr_active_games; i++) {
            if (active_games[i].id == client->current_game_id) {
                handle_forfeit(&active_games[i], client, curr_active_games, active_games);
                memset(response, 0, MAX_BUFFER_SIZE);
                snprintf(response, MAX_BUFFER_SIZE, "FORFEITOK");
                return response;
            }
        }

        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "Impossible de trouver la partie");
    }else if (strcmp(verb, "ISFULL") == 0) {
        int game_id = client->current_game_id;
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

        // Répondre en fonction de l'état
        if (is_full) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "YES");
        } else {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "NO");
        }
    }else if (strcmp(verb, "STATUS") == 0) {
        printf("LOG: STATUS command received from %s\n", client->name);

        if (client->current_game_id == -1) {
            memset(response, 0, MAX_BUFFER_SIZE);
            snprintf(response, MAX_BUFFER_SIZE, "ERROR: No active game");
            return response;
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
                        client->received = 1;
                    } else {
                        memset(response, 0, MAX_BUFFER_SIZE);
                        snprintf(response, MAX_BUFFER_SIZE, "LOSER %s", games_list);
                        client->current_game_id = -1;
                        client->received = 1;
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
                return response;
            }
        }

        memset(response, 0, MAX_BUFFER_SIZE);
        snprintf(response, MAX_BUFFER_SIZE, "ENDED %s", games_list);
        client->current_game_id = -1;
    }else {
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
        clients[i].name[0] = '\0';
        clients[i].wins = 0;
        clients[i].losses = 0;
        clients[i].games_played = 0;
        clients[i].current_game_id = -1;
        clients[i].is_authenticated = 0;
        clients[i].score = 0;
        clients[i].forfeit = 0;
    }
}

void closeconnection(Client *client) {
    close(client->socket_fd);
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
        return false;
    }

    if(client->current_game_id != -1) {
        return false;
    }

    new_game.id = *curr_available_games;
    new_game.player1 = client;
    //new_game.player2 = (Client *)malloc(sizeof(Client));
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
    /*if (new_game.player2 == NULL) {
        perror("Failed to allocate memory for player2");
        return false;
    }*/

    //initializePlayer(new_game.player2);
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
    } else {
        game->player1->wins++;
        game->player2->forfeit++;
    }

    game->winner = (game->player1 == forfeiter) ? 2 : 1;
    game->player1->games_played++;
    game->player2->games_played++;
    game->is_finished = 1;

    //game->player1->current_game_id = -1;
    //game->player2->current_game_id = -1;

    //removeGame(game->id, active_games, curr_active_games);
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

void test_capturePieces() {
    Game game;
    Client player1, player2;

    // Initialize players
    player1.socket_fd = 1;
    player1.is_connected = 1;
    player1.score = 0;
    player1.current_game_id = 0;
    player2.socket_fd = 2;
    player2.is_connected = 1;
    player2.score = 0;
    player2.current_game_id = 0;

    // Initialize game
    game.id = 0;
    game.player1 = &player1;
    game.player2 = &player2;
    game.is_finished = 0;
    game.turn = 1;
    game.player1_captures = 0;
    game.player2_captures = 0;
    for (int i = 0; i < 19; i++) {
        for (int j = 0; j < 19; j++) {
            game.board[i][j] = 0;
        }
    }

    // Set up a board state where a capture should occur
    game.board[10][10] = 1; // Player 1
    game.board[11][11] = 2; // Player 2
    game.board[12][12] = 2; // Player 2
    game.board[13][13] = 1; // Player 1

    // Display the board before capture
    printf("Board before capture:\n");
    for (int i = 0; i < 19; i++) {
        for (int j = 0; j < 19; j++) {
            printf("%d ", game.board[i][j]);
        }
        printf("\n");
    }
    // Make a move that should trigger a capture
    capturePieces(&game, 13, 13, 1);

    // Display the board after capture
    printf("Board after capture:\n");
    for (int i = 0; i < 19; i++) {
        for (int j = 0; j < 19; j++) {
            printf("%d ", game.board[i][j]);
        }
        printf("\n");
    }

    // Check if the pieces were captured
    assert(game.board[11][11] == 0);
    assert(game.board[12][12] == 0);

    // Check if the score was updated correctly
    assert(game.player1_captures == 2);
    assert(game.player1->score == 2);

    printf("test_capturePieces passed\n");
}