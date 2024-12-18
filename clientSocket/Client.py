import socket
import threading

import pygame_gui
import pygame
from pygame_gui.elements import UILabel, UITextEntryLine, UIButton

global show_board

def join_game(game_id, client_socket):
    packet = f"JOIN {game_id}"
    send_packet(packet, client_socket)
    response = receive_packet(client_socket)

    return response

def display_games(games, join_buttons, y_position):
    # Conserver le bouton "Refresh"
    clear_interface(manager)
    refresh_button.show()
    disconnect_button.show()
    create_game_button.show()
    if games == "NONE":
        # Rectangle avec un UILabel centré
        rect_width, rect_height = 400, 100
        rect_x = (800 - rect_width) // 2  # Centré horizontalement
        rect_y = (600 - rect_height) // 2  # Centré verticalement

        # Panneau pour le rectangle gris
        message_panel = pygame_gui.elements.UIPanel(
            relative_rect=pygame.Rect(rect_x, rect_y, rect_width, rect_height),
            starting_height=1,
            manager=manager,
            object_id="#no_games_panel"
        )

        no_games_label = UILabel(
            relative_rect=pygame.Rect(10, 10, rect_width - 20, rect_height - 20),
            text="No games available",
            manager=manager,
            container=message_panel
        )
        return

    # Liste des jeux si `games` n'est pas "NONE"
    list_games = games.split(",")

    # Conteneur défilant
    scrollable_container = pygame_gui.elements.UIScrollingContainer(
        relative_rect=pygame.Rect((100, 100, 600, 400)),  # Taille du conteneur
        manager=manager,
        object_id="#game_list_container"
    )

    # Afficher les jeux disponibles
    for game in list_games:
        # Création des boutons et labels dans le conteneur
        button = draw_game_info(game, (10, y_position), manager, scrollable_container)
        join_buttons.append((button, game.split(";")[0]))  # Associe le bouton à l'ID de la partie
        y_position += 140  # Espace entre les cadres

def handle_connection(client_socket, username, password):
    try:
        packet = f"CONNECT {username} {password}"
        send_packet(packet, client_socket)
        response = receive_packet(client_socket)

        if not response:
            return False, "No response from the server."

        verb = response.split(" ")[0]

        if verb != "OK":
            return False, "Invalid credentials or server error."

        return True, response.split(" ")[1]  # Succès
    except socket.error as e:
        print(f"Erreur de connexion : {e}")
        return False, f"Connection error: {e}"

def send_packet(data, client_socket):
    client_socket.send(data.encode("utf-8"))


def receive_packet(client_socket):
    response = client_socket.recv(1024)

    return response.decode("utf-8")


def init_pygame():
    pygame.init()
    screen = pygame.display.set_mode((800, 600))
    pygame.display.set_caption('Client GUI')
    background = pygame.Surface((800, 600))
    background.fill(pygame.Color("#FFFFFF"))

    manager = pygame_gui.UIManager((800, 600), "theme.json")

    clock = pygame.time.Clock()

    return screen, background, manager, clock

def clear_interface(manager):
    root_container = manager.get_root_container()
    for element in root_container.elements.copy():
        element.hide()

def draw_game_info(game_info, position, manager, container=None):
    game_details = game_info.split(";")
    game_id = game_details[0]
    name1 = game_details[1]
    score1 = game_details[2]
    wins1 = game_details[3]
    losses1 = game_details[4]
    forfeit1 = game_details[5]

    # Cadre pour la partie
    frame = pygame_gui.elements.UIPanel(
        relative_rect=pygame.Rect(position, (580, 120)),
        starting_height=1,
        manager=manager,
        container=container,  # Ajout pour placer dans le conteneur
        object_id="#game_frame"
    )

    # Informations du joueur 1
    player1_label = UILabel(
        relative_rect=pygame.Rect((10, 10), (560, 20)),
        text=f"Player 1: {name1} - Score: {score1} - Wins: {wins1} - Losses: {losses1} - Forfeits: {forfeit1}",
        manager=manager,
        container=frame
    )

    # Bouton pour rejoindre la partie
    join_button = UIButton(
        relative_rect=pygame.Rect((10, 80), (150, 30)),
        text="Join Game",
        manager=manager,
        container=frame
    )
    return join_button

def display_error_message(message, manager):
    # Dimensions et position du rectangle
    rect_width, rect_height = 400, 100
    rect_x = (800 - rect_width) // 2  # Centré horizontalement
    rect_y = (600 - rect_height) // 2  # Centré verticalement

    # Panneau pour le rectangle
    error_panel = pygame_gui.elements.UIPanel(
        relative_rect=pygame.Rect(rect_x, rect_y, rect_width, rect_height),
        starting_height=1,
        manager=manager,
        object_id="#error_panel"
    )

    # Label pour le message d'erreur
    error_label = UILabel(
        relative_rect=pygame.Rect(10, 10, rect_width - 20, rect_height - 20),
        text=message,
        manager=manager,
        container=error_panel
    )

def handle_refresh(response):
    verb = response.split(" ")[0]

    if verb != "OK":
        return False, "Error when refreshing the game list."

    return True, response.split(" ")[1]

def handle_submit_button(client_socket, join_buttons, y_position, connected):
    username = username_input.get_text()
    password = password_input.get_text()
    if username and password:
        response = handle_connection(client_socket, username, password)
        print(f"response : {response}")
        if not response[0]:
            display_error_message("Connection failed: Invalid credentials or server error.",
                                  manager)
        else:
            connected["value"] = True
            display_games(response[1], join_buttons, y_position)
            refresh_button.show()
            create_game_button.show()
            disconnect_button.show()
    else:
        display_error_message("Please enter both username and password.", manager)

def handle_refresh_button(client_socket, join_buttons, y_position):
    # Commande pour rafraîchir la liste des parties
    send_packet("LIST", client_socket)
    response = receive_packet(client_socket)

    if not handle_refresh(response)[0]:
        display_error_message("Error when refreshing the game list.", manager)
    else:
        display_games(handle_refresh(response)[1], join_buttons, y_position)

def handle_join_buttons(event, button, game_id, client_socket, in_game):
    global show_board
    if event.ui_element == button:
        response = join_game(game_id, client_socket)

        if response == "OK":
            print(f"Game {game_id} joined successfully.")
            show_board = True
            in_game["value"] = True
        else:
            print(f"Error when joining game {game_id}.")

def handle_create_button(client_socket, waiting_for_player, in_game):
    send_packet("CREATE", client_socket)
    response = receive_packet(client_socket)

    if response == "OK":
        clear_interface(manager)
        quit_button.show()
        waiting_label.show()
        waiting_for_player["value"] = True
        in_game["value"] = True
    else:
        print(f"response : {response}")

def handle_quit_button(client_socket, join_buttons, y_position, waiting_for_player, in_game):
    global show_board
    if waiting_for_player["value"]:
        send_packet("QUIT", client_socket)
    else:
        send_packet("FORFEIT", client_socket)

    response = receive_packet(client_socket)

    verb = response.split(" ")[0]

    print(f"response : {response}")
    if verb == "OK":
        waiting_for_player["value"] = False
        in_game["value"] = False
        show_board = False
        display_games(response.split(" ")[1], join_buttons, y_position)
        create_game_button.show()
        refresh_button.show()
        disconnect_button.show()

def handle_disconnect_button(client_socket):
    send_packet("DISCONNECT", client_socket)

def handle_board_click(board_state, mouse_x, mouse_y, current_player):
    """
    Gère le clic sur le plateau de jeu et retourne les coordonnées du mouvement effectué.

    Args:
        board_state (list): Matrice 19x19 représentant l'état du plateau.
        mouse_x (int): Position X de la souris au moment du clic.
        mouse_y (int): Position Y de la souris au moment du clic.
        current_player (str): Le joueur actuel ("Black" ou "White").

    Returns:
        tuple: (bool, tuple) - Le booléen indique si le mouvement a été effectué avec succès.
               La deuxième valeur est un tuple (row, col) des coordonnées du coup si réussi, ou None sinon.
    """
    board_size = 19
    cell_size = 30  # Taille d'une cellule en pixels
    screen_width, screen_height = screen.get_size()
    board_pixel_size = board_size * cell_size

    # Calcul des offsets pour centrer le plateau
    x_offset = (screen_width - board_pixel_size) // 2
    y_offset = (screen_height - board_pixel_size) // 2

    # Vérifie si le clic est dans les limites du plateau
    if x_offset <= mouse_x <= x_offset + board_pixel_size and \
            y_offset <= mouse_y <= y_offset + board_pixel_size:

        # Calculer les indices de la case cliquée
        col = (mouse_x - x_offset) // cell_size
        row = (mouse_y - y_offset) // cell_size

        # Vérifie si la cellule est vide
        if board_state[row][col] == "":
            board_state[row][col] = current_player["curr"]
            print(f"Placed {current_player} at ({row}, {col})")
            current_player["curr"] = "Black" if current_player["curr"] == "White" else "White"
            return True, (row, col)  # Mouvement réussi avec coordonnées
        else:
            print(f"Cell ({row}, {col}) is already occupied.")
    else:
        print("Click outside the board.")

    return False, None  # Mouvement échoué


def handle_events(event, client_socket, join_buttons, y_position, empty_board, current_player, connected,
                  waiting_for_player, in_game):
    if event.type == pygame.QUIT:
        is_running = False

    # Gestion des événements liés à PyGame-GUI
    if event.type == pygame_gui.UI_BUTTON_PRESSED:
        if event.ui_element == submit_button:
            handle_submit_button(client_socket, join_buttons, y_position, connected)

        if event.ui_element == refresh_button:
            handle_refresh_button(client_socket, join_buttons, y_position)

        if event.ui_element == create_game_button:
            handle_create_button(client_socket, waiting_for_player, in_game)

        if event.ui_element == quit_button:
            handle_quit_button(client_socket, join_buttons, y_position, waiting_for_player, in_game)

        if event.ui_element == disconnect_button:
            handle_disconnect_button(client_socket)
            return False

        for button, game_id in join_buttons:
            handle_join_buttons(event, button, game_id, client_socket, in_game)

    if connected and show_board:
        if event.type == pygame.MOUSEBUTTONDOWN:  # Détecter un clic de souris
            if handle_board_click(empty_board, event.pos[0], event.pos[1], current_player)[0]:
                move = handle_board_click(empty_board, event.pos[0], event.pos[1], current_player)[1]

    if waiting_for_player["value"]:
        if handle_waiting(client_socket):
            waiting_for_player["value"] = False

    return True

def display_pente_board(screen, board_state):
    """
    Affiche un plateau de jeu de Pente interactif avec gestion des pions.

    Args:
        screen: Surface PyGame où dessiner le plateau.
        board_state: Matrice 19x19 représentant l'état du plateau
                     ("Black", "White", ou "") pour chaque intersection.
    """
    #TODO: display pieces on intersections
    clear_interface(manager)

    # Dimensions du plateau et de la fenêtre
    board_size = 19
    cell_size = 30  # Taille d'une cellule en pixels
    window_width, window_height = screen.get_size()
    board_pixel_size = board_size * cell_size

    # Calcul des offsets pour centrer le plateau
    x_offset = (window_width - board_pixel_size) // 2
    y_offset = (window_height - board_pixel_size) // 2

    # Couleur des lignes de la grille
    grid_color = pygame.Color("#000000")  # Noir

    # Dessiner les lignes horizontales et verticales
    for i in range(board_size + 1):  # Ajouter une ligne supplémentaire pour le bord droit/bas
        # Ligne horizontale
        pygame.draw.line(screen, grid_color,
                         (x_offset, y_offset + i * cell_size),
                         (x_offset + board_pixel_size, y_offset + i * cell_size), 1)
        # Ligne verticale
        pygame.draw.line(screen, grid_color,
                         (x_offset + i * cell_size, y_offset),
                         (x_offset + i * cell_size, y_offset + board_pixel_size), 1)

    # Dessiner les pions
    for row in range(board_size):
        for col in range(board_size):
            # Position absolue de la cellule
            cell_x = x_offset + col * cell_size
            cell_y = y_offset + row * cell_size

            # Récupérer l'état de la cellule
            cell_state = board_state[row][col]

            # Dessiner un pion si nécessaire
            if cell_state == "Black":
                color = pygame.Color("#000000")  # Noir
                border_color = pygame.Color("#FFFFFF")  # Bordure blanche pour le noir
            elif cell_state == "White":
                color = pygame.Color("#FFFFFF")  # Blanc
                border_color = pygame.Color("#000000")  # Bordure noire pour le blanc
            else:
                color = None
                border_color = None

            if color:
                # Dessiner la bordure (cercle plus grand)
                pygame.draw.circle(screen, border_color,
                                   (cell_x + cell_size // 2, cell_y + cell_size // 2),
                                   cell_size // 3 + 2)  # Rayon plus grand pour la bordure

                # Dessiner le pion (cercle plus petit à l'intérieur de la bordure)
                pygame.draw.circle(screen, color,
                                   (cell_x + cell_size // 2, cell_y + cell_size // 2),
                                   cell_size // 3)  # Rayon plus petit pour le pion

def handle_waiting(client_socket):
    send_packet("ISFULL", client_socket)
    response = receive_packet(client_socket)

    print(f"response : {response}")

    if response == "YES":
        return True

    return False

def main_loop():
    # Initialisation de la connexion au serveur
    join_buttons = []
    y_position = 100
    current_player = {"curr": "Black"}  # Par défaut, commence par Noir
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    empty_board = [["" for _ in range(19)] for _ in range(19)]
    global show_board
    show_board = False
    connected = {"value": False}
    waiting_for_player = {"value": False}
    in_game = {"value" : False}
    try:
        client_socket.connect(('127.0.0.1', 55555))
    except ConnectionRefusedError:
        print("Unable to connect to the server.")
        exit()

    is_running = True
    while is_running:
        time_delta = clock.tick(60) / 1000.0

        for event in pygame.event.get():
            if not handle_events(event, client_socket, join_buttons, y_position, empty_board, current_player, connected, waiting_for_player, in_game):
                is_running = False

            manager.process_events(event)

        if not waiting_for_player["value"] and in_game["value"]:
            show_board = True

        manager.update(time_delta)

        screen.blit(background, (0, 0))

        if show_board:
            display_pente_board(screen, empty_board)
            quit_button.show()

        manager.draw_ui(screen)
        pygame.display.update()

    pygame.quit()

# Initialisation de l'interface PyGame
screen, background, manager, clock = init_pygame()

# Ajout des éléments de l'interface graphique
username_label = UILabel(
    relative_rect=pygame.Rect((100, 150), (100, 30)),
    text="Name:",
    manager=manager
)
username_input = UITextEntryLine(
    relative_rect=pygame.Rect((200, 150), (200, 30)),
    manager=manager
)
password_label = UILabel(
    relative_rect=pygame.Rect((100, 200), (100, 30)),
    text="Password:",
    manager=manager
)
password_input = UITextEntryLine(
    relative_rect=pygame.Rect((200, 200), (200, 30)),
    manager=manager
)
password_input.set_text_hidden(True)  # Masquer le texte du champ Password
submit_button = UIButton(
    relative_rect=pygame.Rect((150, 250), (100, 40)),
    text="Submit",
    manager=manager
)

create_game_button = UIButton(
    relative_rect=pygame.Rect((350, 500), (100, 40)),
    text="Create Game",
    manager=manager
)
create_game_button.hide()

# Bouton pour rafraîchir la liste des parties
refresh_button = UIButton(
    relative_rect=pygame.Rect((650, 10), (100, 40)),  # Position dans le coin supérieur droit
    text="Refresh",
    manager=manager,
    object_id="#refresh_button"  # Identifiant unique pour ce bouton
)
refresh_button.hide()

quit_button = UIButton(
    relative_rect=pygame.Rect((10, 10), (100, 40)),  # Position en haut à gauche
    text="Quit",
    manager=manager
)
quit_button.hide()

waiting_label = UILabel(
        relative_rect=pygame.Rect((250, 250), (300, 50)),
        text="Waiting for a worthy opponent...",
        manager=manager
)
waiting_label.hide()

disconnect_button = UIButton(
    relative_rect=pygame.Rect((10, 10), (120, 40)),  # Coin supérieur gauche
    text="Déconnexion",
    manager=manager,
    object_id="#disconnect_button"  # ID unique
)
disconnect_button.hide()

def main():
    main_loop()
    return 0

if __name__ == "__main__":
    main()