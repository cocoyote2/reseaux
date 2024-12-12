import socket
import pygame_gui
import pygame
from pygame_gui.elements import UILabel, UITextEntryLine, UIButton

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
        element.kill()

def draw_game_info(game_info, position, manager):
    game_details = game_info.split(";")
    game_id = game_details[0]
    name1 = game_details[1]
    score1 = game_details[2]
    wins1 = game_details[3]
    losses1 = game_details[4]
    forfeit1 = game_details[5]
    name2 = game_details[6]
    score2 = game_details[7]
    wins2 = game_details[8]
    losses2 = game_details[9]
    forfeit2 = game_details[10]

    game_rect = pygame.Rect(position, (600, 120))
    game_background = pygame.Surface(game_rect.size)
    game_background.fill(pygame.Color("#DDDDDD"))

    screen.blit(game_background, game_rect.topleft)

    game_label = UILabel(
        relative_rect=pygame.Rect((position[0] + 10, position[1] + 10), (580, 20)),
        text=f"Player 1: {name1} - Score1: {score1} - Wins1: {wins1} - Losses1: {losses1} - Forfeit1: {forfeit1}",
        manager=manager
    )

    game_label2 = UILabel(
        relative_rect=pygame.Rect((position[0] + 10, position[1] + 40), (580, 20)),
        text=f"Player 2: {name2} - Score2: {score2} - Wins2: {wins2} - Losses2: {losses2} - Forfeit2: {forfeit2}",
        manager=manager
    )

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

def main_loop():
    # Initialisation de la connexion au serveur
    y_position = 100
    connected = False
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        client_socket.connect(('127.0.0.1', 55555))
    except ConnectionRefusedError:
        print("Unable to connect to the server.")
        exit()

    # Boucle principale
    is_running = True
    while is_running:
        time_delta = clock.tick(60) / 1000.0

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                is_running = False

            # Gestion des événements liés à PyGame-GUI
            if event.type == pygame_gui.UI_BUTTON_PRESSED:
                if event.ui_element == submit_button:
                    username = username_input.get_text()
                    password = password_input.get_text()
                    if username and password:
                        packet = f"CONNECT {username} {password}"
                        send_packet(packet, client_socket)
                        response = receive_packet(client_socket)
                        if response:
                            clear_interface(manager)
                            list_games = response.split(",")

                            print(f"Response: {response}")
                            print(f"List of games: {list_games}")

                            title_label = UILabel(
                                relative_rect=pygame.Rect((400, 0), (200, 30)),
                                text="List of available games:",
                                manager=manager
                            )
                            
                            for game in list_games:
                                curr_game = game.split(";")
                                game_id = curr_game[0]
                                draw_game_info(game, (100, y_position), manager)
                                y_position += 130

                        connected = True
                    else:
                        print("Please enter both username and password.")

            manager.process_events(event)

        # Mise à jour de l'interface
        manager.update(time_delta)

        # Rendu graphique
        screen.blit(background, (0, 0))
        manager.draw_ui(screen)
        pygame.display.update()

    pygame.quit()

def main():
    main_loop()
    return 0

if __name__ == "__main__":
    main()