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

    manager = pygame_gui.UIManager((800, 600))

    clock = pygame.time.Clock()

    return screen, background, manager, clock

def clear_interface(manager):
    root_container = manager.get_root_container()
    for element in root_container.elements.copy():
        element.kill()

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
                                relative_rect=pygame.Rect((100, 100), (200, 30)),
                                text="List of available games:",
                                manager=manager
                            )
                            
                            for game in list_games:
                                curr_game = game.split(";")
                                game_id = curr_game[0]


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