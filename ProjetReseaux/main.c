#define REUSE 1
#define REVDNS 1
#define MAX_CLIENTS 10
#define MAX_GAMES 10
#define MAX_BUFFER_SIZE 512
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
}Client;

typedef struct Game {
    int id;
    Client *player1;
    Client *player2;
    int is_finished;
}Game;

void sendPacket(char* buffer, Client client);

char* processcmd(char *buffer, Client *client, Game *available_games, Game *active_games, int *curr_available_games, int *curr_active_games);

void init_clients(Client clients[]);

void closeconnection(Client *client);

bool createGame(Client *client, Game *available_games, int *curr_available_games);

void formatCommand(char *buffer);

bool joinGame(int game_id, Client *client, Game *available_games, int *curr_available_games, Game *active_games, int *curr_active_games);

void initializePlayer(Client *player);

void displayGameList(int curr_available_games, Game *available_games, char *games_list);

bool removeGame(int game_id, Game *games, int *curr_games);

void finishGame(Game *game);

void handle_forfeit(Game *game, const Client *forfeiter);

bool quit_game(Game *games, int *curr_available_games, const Client *client);

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
                    // Envoyer le message au client
                    char *response = processcmd(buffer, &clients[i], available_games, active_games, &curr_available_games, &curr_active_games);

                    printf("Commande envoyée : %s\n", response);
                    sendPacket(response, clients[i]);

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
        snprintf(response, MAX_BUFFER_SIZE, "Commande invalide : %s", verb);
        return response;
    }else {
        formatCommand(verb);
    }

    if(strcmp(verb, "CONNECT") == 0) {
        char *player_name = strtok(NULL, " ");
        if(client->is_connected == 1) {
            snprintf(response, MAX_BUFFER_SIZE, "Vous êtes déjà connecté");
            return response;
        }

        if(player_name == NULL || strcmp(player_name, "") == 0) {
            snprintf(response, MAX_BUFFER_SIZE, "Pseudonyme invalide");
            return response;
        }

        char *password = strtok(NULL, " ");

        if (password != NULL) {
            formatCommand(password);
        }

        if(password == NULL || strcmp(password, PASSWORD) != 0) {
            snprintf(response, MAX_BUFFER_SIZE, "Mot de passe invalide : %s + %d", password, strcmp(password, PASSWORD));
            return response;
        }

        strcpy(client->name, player_name);
        client->is_connected = 1;

        char games_list[MAX_BUFFER_SIZE];
        displayGameList(*curr_available_games, available_games, games_list);

        snprintf(response, MAX_BUFFER_SIZE, "OK %s", games_list);
    }else if(strcmp(verb, "DISCONNECT") == 0){
        snprintf(response, MAX_BUFFER_SIZE, "Déconnecté.");
    }else if(strcmp(verb, "MOVE") == 0) {
        snprintf(response, MAX_BUFFER_SIZE, "Mouvement");
    }else if(strcmp(verb, "CREATE") == 0) {
        if(!createGame(client, available_games, curr_available_games)) {
            snprintf(response, MAX_BUFFER_SIZE, "Impossible de créer une partie");
            return response;
        }

        snprintf(response, MAX_BUFFER_SIZE, "OK");
    }else if(strcmp(verb, "JOIN") == 0) {
        char *game_id_string = strtok(NULL, " ");
        if(game_id_string == NULL) {
            snprintf(response, MAX_BUFFER_SIZE, "Identifiant de partie invalide");
            return response;
        }

        int game_id = atoi(game_id_string);
        if(game_id >= *curr_available_games) {
            snprintf(response, MAX_BUFFER_SIZE, "Identifiant de partie invalide");
            return response;
        }

        if(!joinGame(game_id, client, available_games, curr_available_games, active_games, curr_active_games)) {
            snprintf(response, MAX_BUFFER_SIZE, "Impossible de rejoindre la partie.");
            return response;
        }

        snprintf(response, MAX_BUFFER_SIZE, "OK");
    }else if(strcmp(verb, "STATS") == 0) {
        snprintf(response, MAX_BUFFER_SIZE, "Stats : Name : %s, Wins : %d, Losses : %d", client->name, client->wins, client->losses);
    }else if(strcmp(verb, "QUIT") == 0) {
        if (!quit_game(available_games, curr_available_games, client)) {
            snprintf(response, MAX_BUFFER_SIZE, "Impossible de quitter la partie");
        }

        char games_list[MAX_BUFFER_SIZE];

        displayGameList(*curr_available_games, available_games, games_list);

        snprintf(response, MAX_BUFFER_SIZE, "OK %s", games_list);
    }else if (strcmp(verb, "LIST") == 0){
        char games_list[MAX_BUFFER_SIZE];
        displayGameList(*curr_available_games, available_games, games_list);

        snprintf(response, MAX_BUFFER_SIZE, "OK %s", games_list);
    }else if (strcmp(verb, "FORFEIT") == 0) {
        if (client->current_game_id == -1) {
            snprintf(response, MAX_BUFFER_SIZE, "Vous n'êtes pas dans une partie");
            return response;
        }

        for (int i = 0; i < *curr_active_games; i++) {
            if (active_games[i].id == client->current_game_id) {
                handle_forfeit(&active_games[i], client);
                snprintf(response, MAX_BUFFER_SIZE, "Vous avez abandonné la partie");
                return response;
            }
        }

        snprintf(response, MAX_BUFFER_SIZE, "Impossible de trouver la partie");

    }else {
        snprintf(response, MAX_BUFFER_SIZE, "Commande invalide : %s + length : %lu", verb, strlen(verb));
    }

    return response;
}

void sendPacket(const char* buffer, const Client client) {
    if(client.socket_fd > 0) {
        send(client.socket_fd, buffer, strlen(buffer), 0);
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
    new_game.player2 = (Client *)malloc(sizeof(Client));
    if (new_game.player2 == NULL) {
        perror("Failed to allocate memory for player2");
        return false;
    }

    initializePlayer(new_game.player2);
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

bool joinGame(int game_id, Client *client, Game *available_games, const int *curr_available_games, Game *active_games, int *curr_active_games) {
    if(client->current_game_id != -1) {
        return false;
    }

    for(int i = 0;i<*curr_available_games;i++) {
        if(available_games[i].id == game_id) {
            if(available_games[i].player2->socket_fd != 0) {
                return false;
            }

            available_games[i].player2 = client;
            client->current_game_id = game_id;

            active_games[*curr_active_games] = available_games[i];
            (*curr_active_games)++;

            //finishGame(&available_games[i]);

            //removeGame(game_id, available_games, curr_available_games);

            return true;
        }
    }

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

void finishGame(Game *game) {
    int const winner = rand() % 2;

    game->is_finished = 1;
    game->player1->current_game_id = -1;
    game->player2->current_game_id = -1;

    if (winner == 0) {
        game->player1->wins++;
        game->player2->losses++;
    } else {
        game->player2->wins++;
        game->player1->losses++;
    }

    game->player1->games_played++;
    game->player2->games_played++;
}

void handle_forfeit(Game *game, const Client *forfeiter) {
    if (game->player1 == forfeiter) {
        game->player2->wins++;
        game->player1->forfeit++;
    } else {
        game->player1->wins++;
        game->player2->forfeit++;
    }

    game->player1->games_played++;
    game->player2->games_played++;
    game->is_finished = 1;

    game->player1->current_game_id = -1;
    game->player2->current_game_id = -1;
}

bool quit_game(Game *games, int *curr_available_games, const Client *client) {
    for (int i = 0; i < *curr_available_games; i++) {
        if (games[i].player1 == client) {
            games[i].player1->current_game_id = -1;
            games[i].player2->current_game_id = -1;
            (*curr_available_games)--;

            if (removeGame(i, games, curr_available_games)) {
                return true;
            }
        }
    }

    return false;
}