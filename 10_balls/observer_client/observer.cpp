#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "../common/network_utils.h"
#include "../common/protocol.h"

class ObserverClient {
   private:
    int sockfd;
    std::string server_ip;
    int server_port;
    int client_port;
    int client_id;
    bool running;
    bool registered;
    std::string accumulated_status;

    static ObserverClient* instance;

   public:
    ObserverClient(const std::string& server_ip, int server_port, int client_port)
        : server_ip(server_ip),
          server_port(server_port),
          client_port(client_port),
          client_id(0),
          running(false),
          registered(false) {
        instance = this;
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
    }

    static void signalHandler(int signal) {
        if (instance) {
            std::cout << "\nПолучен сигнал завершения..." << std::endl;
            instance->disconnect();
        }
    }

    bool start() {
        sockfd = NetworkUtils::createUDPSocket();
        if (sockfd < 0) {
            return false;
        }

        if (!NetworkUtils::bindSocket(sockfd, "0.0.0.0", client_port)) {
            close(sockfd);
            return false;
        }

        std::cout << "Наблюдатель запущен на порту " << client_port << std::endl;

        if (!registerWithServer()) {
            close(sockfd);
            return false;
        }

        running = true;

        // Запускаем потоки
        std::thread message_thread(&ObserverClient::messageLoop, this);
        std::thread input_thread(&ObserverClient::inputLoop, this);

        // Ждем завершения
        message_thread.join();
        input_thread.join();

        return true;
    }

    void disconnect() {
        if (!running)
            return;

        std::cout << "Отключаемся от сервера..." << std::endl;

        if (registered) {
            Message msg;
            msg.type = DISCONNECT;
            msg.client_id = client_id;
            strcpy(msg.data, "Observer disconnecting");

            NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port);
        }

        running = false;
        close(sockfd);
    }

   private:
    bool registerWithServer() {
        Message msg;
        msg.type = REGISTER_OBSERVER;
        msg.client_id = 0;  // Будет присвоен сервером
        strcpy(msg.data, "Observer client");

        if (!NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port)) {
            std::cout << "Ошибка отправки регистрации на сервер" << std::endl;
            return false;
        }

        // Ждем ответа от сервера
        std::string from_ip;
        int from_port;
        auto start_time = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(10)) {
            if (NetworkUtils::receiveMessage(sockfd, msg, from_ip, from_port)) {
                if (msg.type == REGISTER_OBSERVER) {
                    client_id = msg.client_id;
                    registered = true;
                    std::cout << "Зарегистрированы на сервере с ID: " << client_id << std::endl;
                    std::cout << "\nДоступные команды:" << std::endl;
                    std::cout << "  q - выход" << std::endl;
                    std::cout << "  r - обновить статус" << std::endl;
                    std::cout << "  h - помощь" << std::endl;
                    std::cout << "\nНажмите Enter для просмотра текущего статуса..." << std::endl;
                    return true;
                }
            }
            usleep(100000);  // 100ms
        }

        std::cout << "Таймаут регистрации на сервере" << std::endl;
        return false;
    }

    void messageLoop() {
        while (running) {
            processMessages();
            usleep(100000);  // 100ms
        }
    }

    void inputLoop() {
        while (running) {
            char input = getchar();

            switch (input) {
                case 'q':
                case 'Q':
                    std::cout << "Завершение работы наблюдателя..." << std::endl;
                    disconnect();
                    return;

                case 'r':
                case 'R':
                    std::cout << "Запрос обновления статуса..." << std::endl;
                    requestStatusUpdate();
                    break;

                case 'h':
                case 'H':
                    printHelp();
                    break;

                case '\n':
                    requestStatusUpdate();
                    break;

                default:
                    break;
            }
        }
    }

    void processMessages() {
        Message msg;
        std::string from_ip;
        int from_port;

        while (NetworkUtils::receiveMessage(sockfd, msg, from_ip, from_port)) {
            switch (msg.type) {
                case STATUS_UPDATE:
                    handleStatusUpdate(msg);
                    break;
                case SHUTDOWN:
                    handleShutdown(msg);
                    break;
                default:
                    break;
            }
        }
    }

    void handleStatusUpdate(const Message& msg) {
        if (msg.client_id != client_id)
            return;

        if (strcmp(msg.data, "END_OF_STATUS") == 0) {
            // Конец сообщения статуса - выводим накопленную информацию
            clearScreen();
            std::cout << accumulated_status << std::endl;
            std::cout << "Команды: (q)uit, (r)efresh, (h)elp, Enter - обновить" << std::endl;
            accumulated_status.clear();
        } else {
            // Накапливаем части сообщения
            accumulated_status += std::string(msg.data);
        }
    }

    void handleShutdown(const Message& msg) {
        std::cout << "\n🛑 Получена команда завершения от сервера: " << msg.data << std::endl;
        running = false;
    }

    void requestStatusUpdate() {
        if (!registered)
            return;

        // Сервер автоматически отправляет обновления статуса,
        // но мы можем запросить принудительное обновление
        Message msg;
        msg.type = STATUS_UPDATE;
        msg.client_id = client_id;
        strcpy(msg.data, "Request status update");

        NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port);
    }

    void clearScreen() {
        // Очищаем экран (работает в большинстве терминалов)
        std::cout << "\033[2J\033[H";
        std::cout.flush();
    }

    void printHelp() {
        std::cout << "\n=== ПОМОЩЬ ===" << std::endl;
        std::cout << "Доступные команды:" << std::endl;
        std::cout << "  q - Выход из программы" << std::endl;
        std::cout << "  r - Принудительное обновление статуса" << std::endl;
        std::cout << "  h - Показать эту справку" << std::endl;
        std::cout << "  Enter - Обновить статус" << std::endl;
        std::cout << "\nСистема автоматически обновляет статус при изменениях." << std::endl;
        std::cout << "================\n" << std::endl;
    }
};

ObserverClient* ObserverClient::instance = nullptr;

// Функция для установки неблокирующего ввода
void setNonBlockingInput() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void restoreInput() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cout << "Использование: " << argv[0] << " <SERVER_IP> <SERVER_PORT> <CLIENT_PORT>"
                  << std::endl;
        std::cout << "Пример: " << argv[0] << " 127.0.0.1 8080 8090" << std::endl;
        return 1;
    }

    std::string server_ip = argv[1];
    int server_port = std::atoi(argv[2]);
    int client_port = std::atoi(argv[3]);

    if (server_port <= 0 || server_port > 65535 || client_port <= 0 || client_port > 65535) {
        std::cout << "Ошибка: некорректный порт" << std::endl;
        return 1;
    }

    // Устанавливаем неблокирующий ввод
    setNonBlockingInput();

    ObserverClient client(server_ip, server_port, client_port);

    bool result = client.start();

    // Восстанавливаем нормальный ввод
    restoreInput();

    if (!result) {
        std::cout << "Ошибка запуска клиента-наблюдателя" << std::endl;
        return 1;
    }

    return 0;
}
