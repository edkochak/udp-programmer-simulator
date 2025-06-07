#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <vector>

#include "../common/network_utils.h"
#include "../common/protocol.h"

class ProgrammersServer {
   private:
    int sockfd;
    std::string server_ip;
    int server_port;
    bool running;

    // Данные о клиентах
    std::map<int, ProgrammerInfo> programmers;
    std::map<int, std::pair<std::string, int>> programmer_addresses;  // id -> (ip, port)
    std::map<int, std::pair<std::string, int>> observer_addresses;    // id -> (ip, port)

    // Очереди проверок для каждого программиста
    std::map<int, std::queue<ProgramReview>> review_queues;

    // Счетчики
    int next_programmer_id;
    int next_observer_id;
    int next_program_id;

    // Генератор случайных чисел
    std::random_device rd;
    std::mt19937 gen;

    static ProgrammersServer* instance;

   public:
    ProgrammersServer(const std::string& ip, int port)
        : server_ip(ip),
          server_port(port),
          running(false),
          next_programmer_id(1),
          next_observer_id(1000),
          next_program_id(1),
          gen(rd()) {
        instance = this;
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
    }

    static void signalHandler(int signal) {
        if (instance) {
            std::cout << "\nПолучен сигнал завершения. Останавливаем сервер..." << std::endl;
            instance->shutdown();
        }
    }

    bool start() {
        sockfd = NetworkUtils::createUDPSocket();
        if (sockfd < 0) {
            return false;
        }

        if (!NetworkUtils::bindSocket(sockfd, server_ip, server_port)) {
            close(sockfd);
            return false;
        }

        std::cout << "Сервер запущен на " << server_ip << ":" << server_port << std::endl;
        std::cout << "Для завершения работы нажмите Ctrl+C" << std::endl;

        running = true;
        mainLoop();

        return true;
    }

    void shutdown() {
        if (!running)
            return;

        std::cout << "Отправляем команду завершения всем клиентам..." << std::endl;

        // Отправляем SHUTDOWN всем подключенным клиентам
        Message shutdown_msg;
        shutdown_msg.type = SHUTDOWN;
        shutdown_msg.client_id = 0;  // от сервера
        strcpy(shutdown_msg.data, "Server is shutting down");

        // Отправляем программистам
        for (const auto& pair : programmer_addresses) {
            NetworkUtils::sendMessage(sockfd, shutdown_msg, pair.second.first, pair.second.second);
        }

        // Отправляем наблюдателям
        for (const auto& pair : observer_addresses) {
            NetworkUtils::sendMessage(sockfd, shutdown_msg, pair.second.first, pair.second.second);
        }

        // Даем время клиентам обработать команду
        sleep(2);

        running = false;
        close(sockfd);
        std::cout << "Сервер остановлен." << std::endl;
    }

   private:
    void mainLoop() {
        while (running) {
            processMessages();
            checkHeartbeats();
            usleep(100000);  // 100ms
        }
    }

    void processMessages() {
        Message msg;
        std::string from_ip;
        int from_port;

        while (NetworkUtils::receiveMessage(sockfd, msg, from_ip, from_port)) {
            NetworkUtils::printMessage("Получено: ", msg);

            switch (msg.type) {
                case REGISTER_PROGRAMMER:
                    handleRegisterProgrammer(msg, from_ip, from_port);
                    break;
                case REGISTER_OBSERVER:
                    handleRegisterObserver(msg, from_ip, from_port);
                    break;
                case SUBMIT_PROGRAM:
                    handleSubmitProgram(msg, from_ip, from_port);
                    break;
                case REQUEST_REVIEW:
                    handleRequestReview(msg, from_ip, from_port);
                    break;
                case REVIEW_RESULT:
                    handleReviewResult(msg, from_ip, from_port);
                    break;
                case DISCONNECT:
                    handleDisconnect(msg, from_ip, from_port);
                    break;
                case HEARTBEAT:
                    handleHeartbeat(msg, from_ip, from_port);
                    break;
                default:
                    std::cout << "Неизвестный тип сообщения: " << msg.type << std::endl;
            }
        }
    }

    void handleRegisterProgrammer(const Message& msg, const std::string& ip, int port) {
        int id = next_programmer_id++;
        std::string name = std::string(msg.data);
        if (name.empty()) {
            name = "Программист" + std::to_string(id);
        }

        programmers[id] = ProgrammerInfo(id, name);
        programmer_addresses[id] = std::make_pair(ip, port);
        review_queues[id] = std::queue<ProgramReview>();

        // Отправляем подтверждение с присвоенным ID
        Message response;
        response.type = REGISTER_PROGRAMMER;
        response.client_id = id;
        strcpy(response.data, name.c_str());

        NetworkUtils::sendMessage(sockfd, response, ip, port);

        std::cout << "Зарегистрирован программист " << name << " (ID: " << id << ") с адреса " << ip
                  << ":" << port << std::endl;

        broadcastStatusUpdate();
    }

    void handleRegisterObserver(const Message& msg, const std::string& ip, int port) {
        int id = next_observer_id++;
        observer_addresses[id] = std::make_pair(ip, port);

        // Отправляем подтверждение с присвоенным ID
        Message response;
        response.type = REGISTER_OBSERVER;
        response.client_id = id;
        strcpy(response.data, "Observer registered");

        NetworkUtils::sendMessage(sockfd, response, ip, port);

        std::cout << "Зарегистрирован наблюдатель (ID: " << id << ") с адреса " << ip << ":" << port
                  << std::endl;

        // Отправляем текущее состояние
        sendFullStatusToObserver(id);
    }

    void handleSubmitProgram(const Message& msg, const std::string& ip, int port) {
        int author_id = msg.client_id;
        int target_id = msg.target_id;

        if (programmers.find(author_id) == programmers.end() ||
            programmers.find(target_id) == programmers.end()) {
            std::cout << "Ошибка: неизвестный программист" << std::endl;
            return;
        }

        // Создаем программу для проверки
        int program_id = next_program_id++;
        std::string program_name = std::string(msg.data);
        if (program_name.empty()) {
            program_name = "Программа" + std::to_string(program_id);
        }

        ProgramReview review(program_id, author_id, target_id, program_name);
        review_queues[target_id].push(review);

        // Обновляем состояние автора
        programmers[author_id].state = WAITING_REVIEW;
        programmers[author_id].current_program_id = program_id;
        programmers[author_id].current_activity = "Ожидает проверки программы " + program_name;
        programmers[author_id].last_activity = time(nullptr);

        std::cout << "Программист " << programmers[author_id].name << " отправил программу '"
                  << program_name << "' на проверку программисту " << programmers[target_id].name
                  << std::endl;

        // Уведомляем получателя о новой программе для проверки
        if (programmer_addresses.find(target_id) != programmer_addresses.end()) {
            Message notification;
            notification.type = ASSIGNMENT_NOTIFICATION;
            notification.client_id = target_id;
            notification.program_id = program_id;
            notification.target_id = author_id;
            strcpy(notification.data, program_name.c_str());

            auto& addr = programmer_addresses[target_id];
            NetworkUtils::sendMessage(sockfd, notification, addr.first, addr.second);
        }

        broadcastStatusUpdate();
    }

    void handleRequestReview(const Message& msg, const std::string& ip, int port) {
        int reviewer_id = msg.client_id;

        if (programmers.find(reviewer_id) == programmers.end()) {
            return;
        }

        // Проверяем, есть ли программы для проверки
        if (review_queues[reviewer_id].empty()) {
            // Нет программ для проверки
            Message response;
            response.type = REQUEST_REVIEW;
            response.client_id = reviewer_id;
            response.program_id = 0;  // Нет программ
            strcpy(response.data, "No programs to review");

            NetworkUtils::sendMessage(sockfd, response, ip, port);
            return;
        }

        // Берем программу из очереди
        ProgramReview review = review_queues[reviewer_id].front();
        review_queues[reviewer_id].pop();

        // Отправляем программу для проверки
        Message response;
        response.type = REQUEST_REVIEW;
        response.client_id = reviewer_id;
        response.program_id = review.program_id;
        response.target_id = review.author_id;
        strcpy(response.data, review.program_name.c_str());

        NetworkUtils::sendMessage(sockfd, response, ip, port);

        // Обновляем состояние рецензента
        programmers[reviewer_id].state = REVIEWING;
        programmers[reviewer_id].current_activity =
            "Проверяет программу '" + review.program_name + "'";
        programmers[reviewer_id].last_activity = time(nullptr);

        std::cout << "Программист " << programmers[reviewer_id].name
                  << " начал проверку программы '" << review.program_name << "' от "
                  << programmers[review.author_id].name << std::endl;

        broadcastStatusUpdate();
    }

    void handleReviewResult(const Message& msg, const std::string& ip, int port) {
        int reviewer_id = msg.client_id;
        int author_id = msg.target_id;
        int program_id = msg.program_id;
        ReviewResult result = msg.result;

        if (programmers.find(reviewer_id) == programmers.end() ||
            programmers.find(author_id) == programmers.end()) {
            return;
        }

        // Обновляем статистику рецензента
        programmers[reviewer_id].programs_reviewed++;
        programmers[reviewer_id].state = WRITING;
        programmers[reviewer_id].current_activity = "Пишет новую программу";
        programmers[reviewer_id].last_activity = time(nullptr);

        // Пересылаем результат автору
        if (programmer_addresses.find(author_id) != programmer_addresses.end()) {
            auto& addr = programmer_addresses[author_id];
            NetworkUtils::sendMessage(sockfd, msg, addr.first, addr.second);
        }

        std::string result_str = (result == CORRECT) ? "ПРАВИЛЬНО" : "НЕПРАВИЛЬНО";
        std::cout << "Программист " << programmers[reviewer_id].name
                  << " проверил программу (ID: " << program_id << ") - результат: " << result_str
                  << std::endl;

        // Обновляем состояние автора
        if (result == CORRECT) {
            programmers[author_id].programs_written++;
            programmers[author_id].state = WRITING;
            programmers[author_id].current_activity = "Пишет новую программу";
        } else {
            programmers[author_id].state = FIXING;
            programmers[author_id].current_activity =
                "Исправляет программу (ID: " + std::to_string(program_id) + ")";
        }
        programmers[author_id].last_activity = time(nullptr);

        broadcastStatusUpdate();
    }

    void handleDisconnect(const Message& msg, const std::string& ip, int port) {
        int client_id = msg.client_id;

        if (programmers.find(client_id) != programmers.end()) {
            programmers[client_id].is_connected = false;
            std::cout << "Программист " << programmers[client_id].name << " (ID: " << client_id
                      << ") отключился" << std::endl;
        } else if (observer_addresses.find(client_id) != observer_addresses.end()) {
            observer_addresses.erase(client_id);
            std::cout << "Наблюдатель (ID: " << client_id << ") отключился" << std::endl;
        }

        broadcastStatusUpdate();
    }

    void handleHeartbeat(const Message& msg, const std::string& ip, int port) {
        int client_id = msg.client_id;

        if (programmers.find(client_id) != programmers.end()) {
            programmers[client_id].last_activity = time(nullptr);
            programmers[client_id].is_connected = true;
        }
    }

    void checkHeartbeats() {
        time_t now = time(nullptr);

        for (auto& pair : programmers) {
            if (pair.second.is_connected && (now - pair.second.last_activity) > CLIENT_TIMEOUT) {
                pair.second.is_connected = false;
                std::cout << "Программист " << pair.second.name << " (ID: " << pair.first
                          << ") отключился по таймауту" << std::endl;
                broadcastStatusUpdate();
            }
        }
    }

    void broadcastStatusUpdate() {
        for (const auto& pair : observer_addresses) {
            sendFullStatusToObserver(pair.first);
        }
    }

    void sendFullStatusToObserver(int observer_id) {
        if (observer_addresses.find(observer_id) == observer_addresses.end()) {
            return;
        }

        // Создаем сводный статус
        std::string status = "=== СОСТОЯНИЕ СИСТЕМЫ ===\n";
        status += "Время: " + NetworkUtils::getCurrentTime() + "\n\n";

        for (const auto& pair : programmers) {
            const ProgrammerInfo& info = pair.second;
            status += "Программист: " + info.name + " (ID: " + std::to_string(info.id) + ")\n";
            status += "  Состояние: ";

            switch (info.state) {
                case WRITING:
                    status += "Пишет программу";
                    break;
                case WAITING_REVIEW:
                    status += "Ожидает проверки";
                    break;
                case REVIEWING:
                    status += "Проверяет программу";
                    break;
                case FIXING:
                    status += "Исправляет программу";
                    break;
                case SLEEPING:
                    status += "Спит";
                    break;
            }

            status += "\n  Подключен: " + std::string(info.is_connected ? "Да" : "Нет") + "\n";
            status += "  Текущая активность: " + info.current_activity + "\n";
            status += "  Написано программ: " + std::to_string(info.programs_written) + "\n";
            status += "  Проверено программ: " + std::to_string(info.programs_reviewed) + "\n";
            status += "  Программ в очереди на проверку: " +
                      std::to_string(review_queues.at(info.id).size()) + "\n\n";
        }

        Message status_msg;
        status_msg.type = STATUS_UPDATE;
        status_msg.client_id = observer_id;

        // Отправляем статус порциями, если он слишком большой
        size_t pos = 0;
        int part = 1;

        while (pos < status.length()) {
            size_t chunk_size = std::min(sizeof(status_msg.data) - 1, status.length() - pos);
            memset(status_msg.data, 0, sizeof(status_msg.data));
            status.copy(status_msg.data, chunk_size, pos);
            status_msg.program_id = part++;

            auto& addr = observer_addresses[observer_id];
            NetworkUtils::sendMessage(sockfd, status_msg, addr.first, addr.second);

            pos += chunk_size;

            if (pos < status.length()) {
                usleep(10000);  // 10ms задержка между частями
            }
        }

        // Отправляем маркер конца сообщения
        memset(status_msg.data, 0, sizeof(status_msg.data));
        strcpy(status_msg.data, "END_OF_STATUS");
        status_msg.program_id = 0;
        auto& addr = observer_addresses[observer_id];
        NetworkUtils::sendMessage(sockfd, status_msg, addr.first, addr.second);
    }
};

ProgrammersServer* ProgrammersServer::instance = nullptr;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Использование: " << argv[0] << " <IP> <PORT>" << std::endl;
        std::cout << "Пример: " << argv[0] << " 127.0.0.1 8080" << std::endl;
        return 1;
    }

    std::string server_ip = argv[1];
    int server_port = std::atoi(argv[2]);

    if (server_port <= 0 || server_port > 65535) {
        std::cout << "Ошибка: некорректный порт" << std::endl;
        return 1;
    }

    ProgrammersServer server(server_ip, server_port);

    if (!server.start()) {
        std::cout << "Ошибка запуска сервера" << std::endl;
        return 1;
    }

    return 0;
}
