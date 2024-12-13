import socket
import pygame_gui
import pygame
from pygame_gui.elements import UILabel, UITextEntryLine, UIButton

def display_waiting_message(manager):
    # Supprime tous les éléments sauf le message
    clear_interface(manager)
    waiting_label = UILabel(
        relative_rect=pygame.Rect((250, 250), (300, 50)),
        text="Waiting for a worthy opponent...",
        manager=manager
    )

def parse_board(response):
    # Exemple de réponse : "BOARD:Black,,,,,;White,,,,,;,,,,,"
    board_lines = response.split(":")[1].split(";")
    return [line.split(",") for line in board_lines]

def display_board(manager, board_state, current_player, client_socket):
    """
    Affiche le plateau de jeu et gère les interactions.

    Args:
        manager: UIManager de pygame_gui.
        board_state: Matrice 19x19 représentant l'état du plateau.
        current_player: Joueur actuel ("Black" ou "White").
        client_socket: Socket pour la communication serveur.
    """
    # Taille du plateau
    board_size = 19
    cell_size = 30
    board_offset = 50  # Décalage pour centrer le plateau
    board_panel = pygame_gui.elements.UIPanel(
        relative_rect=pygame.Rect((board_offset, board_offset), (board_size * cell_size, board_size * cell_size)),
        starting_height=1,
        manager=manager,
        object_id="#board_panel"
    )

    # Affiche le plateau
    for row in range(board_size):
        for col in range(board_size):
            cell_color = "#FFFFFF"  # Blanc par défaut
            if board_state[row][col] == "Black":
                cell_color = "#000000"
            elif board_state[row][col] == "White":
                cell_color = "#FFFFFF"
            else:
                cell_color = "#D3D3D3"  # Gris clair pour les cases vides

            # Création d'une case
            pygame_gui.elements.UIPanel(
                relative_rect=pygame.Rect((col * cell_size, row * cell_size), (cell_size, cell_size)),
                starting_height=2,
                manager=manager,
                container=board_panel,
                object_id=None
            )

    # Bouton Abandonner
    abandon_button = pygame_gui.elements.UIButton(
        relative_rect=pygame.Rect((700, 10), (100, 30)),
        text="Abandon",
        manager=manager
    )

    return board_panel, abandon_button



def join_game(game_id, client_socket):
    packet = f"JOIN {game_id}"
    send_packet(packet, client_socket)
    response = receive_packet(client_socket)

    return response

def display_games(games, join_buttons, y_position, refresh_button):
    # Conserver le bouton "Refresh"
    clear_interface(manager, preserve=[refresh_button])

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

        # Label pour le message
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

def clear_interface(manager, preserve=[]):
    root_container = manager.get_root_container()
    for element in root_container.elements.copy():
        # Conserve les éléments spécifiés dans la liste `preserve`
        if element in preserve:
            continue
        element.kill()

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

def handle_submit_button(client_socket, join_buttons, y_position):
    username = username_input.get_text()
    password = password_input.get_text()
    if username and password:
        response = handle_connection(client_socket, username, password)
        print(f"response : {response}")
        if not response[0]:
            display_error_message("Connection failed: Invalid credentials or server error.",
                                  manager)
        else:
            display_games(response[1], join_buttons, y_position, refresh_button)
            refresh_button.show()
            connected = True
    else:
        display_error_message("Please enter both username and password.", manager)

def handle_refresh_button(client_socket, join_buttons, y_position):
    # Commande pour rafraîchir la liste des parties
    send_packet("LIST", client_socket)
    response = receive_packet(client_socket)

    if not handle_refresh(response)[0]:
        display_error_message("Error when refreshing the game list.", manager)
    else:
        display_games(handle_refresh(response)[1], join_buttons, y_position, refresh_button)

def handle_join_buttons(event, button, game_id, client_socket):
    if event.ui_element == button:
        # Si on rejoint une partie existante
        send_packet(f"JOIN {game_id}", client_socket)
        response = receive_packet(client_socket)
        if response.startswith("START"):
            board_state = parse_board(response)  # Initialise le plateau
            waiting_for_player = False
        else:
            display_error_message("Failed to join the game.", manager)

def handle_events(event, client_socket, join_buttons, y_position):
    if event.type == pygame.QUIT:
        is_running = False

    # Gestion des événements liés à PyGame-GUI
    if event.type == pygame_gui.UI_BUTTON_PRESSED:
        if event.ui_element == submit_button:
            handle_submit_button(client_socket, join_buttons, y_position)

        if event.ui_element == refresh_button:
            handle_refresh_button(client_socket, join_buttons, y_position)

        for button, game_id in join_buttons:
            handle_join_buttons(event, button, game_id, client_socket)

def main_loop():
    # Initialisation de la connexion au serveur
    join_buttons = []
    y_position = 100
    connected = False
    waiting_for_player = False  # Indique si on attend un adversaire
    current_player = "Black"  # Par défaut, commence par Noir
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    # Plateau de jeu initial (19x19 cases vides)
    board_state = [["" for _ in range(19)] for _ in range(19)]
    board_panel, abandon_button = None, None

    try:
        client_socket.connect(('127.0.0.1', 55555))
    except ConnectionRefusedError:
        print("Unable to connect to the server.")
        exit()

    is_running = True
    while is_running:
        time_delta = clock.tick(60) / 1000.0

        for event in pygame.event.get():
            handle_events(event, client_socket, join_buttons, y_position)
            # Gérer l'affichage si on attend un joueur
            if waiting_for_player:
                send_packet("CHECK_PLAYERS", client_socket)
                response = receive_packet(client_socket)

                if response == "READY":
                    waiting_for_player = False  # Les deux joueurs sont connectés

            # Gérer les clics sur le plateau uniquement si le plateau est visible
            if not waiting_for_player and board_panel is not None:
                if event.type == pygame.MOUSEBUTTONDOWN:
                    mouse_x, mouse_y = event.pos
                    col = (mouse_x - 50) // 30
                    row = (mouse_y - 50) // 30

                    if 0 <= row < 19 and 0 <= col < 19:
                        if board_state[row][col] == "":
                            board_state[row][col] = current_player
                            send_packet(f"PLAY {row} {col}", client_socket)

                            response = receive_packet(client_socket)
                            if response.startswith("BOARD"):
                                board_state = parse_board(response)

                            current_player = "White" if current_player == "Black" else "Black"

            manager.process_events(event)

        # Mise à jour de l'interface
        manager.update(time_delta)

        # Rendu graphique
        screen.blit(background, (0, 0))

        if waiting_for_player:
            display_waiting_message(manager)  # Affiche un message "En attente d'un joueur"
        elif not waiting_for_player and board_panel is not None:
            board_panel, abandon_button = display_board(manager, board_state, current_player, client_socket)

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

# Bouton pour rafraîchir la liste des parties
refresh_button = UIButton(
    relative_rect=pygame.Rect((650, 10), (100, 40)),  # Position dans le coin supérieur droit
    text="Refresh",
    manager=manager,
    object_id="#refresh_button"  # Identifiant unique pour ce bouton
)
refresh_button.hide()

create_game_button = UIButton(
    relative_rect=pygame.Rect((350, 250), (100, 40)),
    text="Create Game",
    manager=manager
)
create_game_button.hide()

refresh_button.hide()
def main():
    main_loop()
    return 0

if __name__ == "__main__":
    main()