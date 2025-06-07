#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "../common/network_utils.h"
#include "../common/protocol.h"

class ProgrammerClient {
   private:
    int sockfd;
    std::string server_ip;
    int server_port;
    int client_port;
    int client_id;
    std::string programmer_name;
    bool running;
    bool registered;

    ProgrammerState current_state;
    int current_program_id;
    int programs_written;
    int programs_reviewed;
    int review_target_id;  // Кому отправлять исправленную программу

    // Генератор случайных чисел
    std::random_device rd;
    std::mt19937 gen;

    static ProgrammerClient* instance;

   public:
    ProgrammerClient(const std::string& name,
                     const std::string& server_ip,
                     int server_port,
                     int client_port)
        : programmer_name(name),
          server_ip(server_ip),
          server_port(server_port),
          client_port(client_port),
          client_id(0),
          running(false),
          registered(false),
          current_state(WRITING),
          current_program_id(0),
          programs_written(0),
          programs_reviewed(0),
          review_target_id(0),
          gen(rd()) {
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

        std::cout << "Программист '" << programmer_name << "' запущен на порту " << client_port
                  << std::endl;

        if (!registerWithServer()) {
            close(sockfd);
            return false;
        }

        running = true;

        // Запускаем потоки
        std::thread message_thread(&ProgrammerClient::messageLoop, this);
        std::thread work_thread(&ProgrammerClient::workLoop, this);
        std::thread heartbeat_thread(&ProgrammerClient::heartbeatLoop, this);

        // Ждем завершения
        message_thread.join();
        work_thread.join();
        heartbeat_thread.join();

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
            strcpy(msg.data, "Client disconnecting");

            NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port);
        }

        running = false;
        close(sockfd);
    }

   private:
    bool registerWithServer() {
        Message msg;
        msg.type = REGISTER_PROGRAMMER;
        msg.client_id = 0;  // Будет присвоен сервером
        strcpy(msg.data, programmer_name.c_str());

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
                if (msg.type == REGISTER_PROGRAMMER) {
                    client_id = msg.client_id;
                    registered = true;
                    std::cout << "Зарегистрированы на сервере с ID: " << client_id << std::endl;
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

    void workLoop() {
        while (running) {
            performWork();
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    void heartbeatLoop() {
        while (running && registered) {
            sendHeartbeat();
            std::this_thread::sleep_for(std::chrono::seconds(HEARTBEAT_INTERVAL));
        }
    }

    void processMessages() {
        Message msg;
        std::string from_ip;
        int from_port;

        while (NetworkUtils::receiveMessage(sockfd, msg, from_ip, from_port)) {
            switch (msg.type) {
                case REVIEW_RESULT:
                    handleReviewResult(msg);
                    break;
                case REQUEST_REVIEW:
                    handleReviewAssignment(msg);
                    break;
                case ASSIGNMENT_NOTIFICATION:
                    handleAssignmentNotification(msg);
                    break;
                case SHUTDOWN:
                    handleShutdown(msg);
                    break;
                default:
                    // Игнорируем остальные сообщения
                    break;
            }
        }
    }

    void handleReviewResult(const Message& msg) {
        if (msg.target_id != client_id)
            return;

        current_program_id = msg.program_id;

        if (msg.result == CORRECT) {
            std::cout << "✓ Программа " << current_program_id << " принята! Пишу новую программу."
                      << std::endl;
            current_state = WRITING;
            programs_written++;
        } else {
            std::cout << "✗ Программа " << current_program_id << " отклонена. Исправляю..."
                      << std::endl;
            current_state = FIXING;
            review_target_id = msg.client_id;  // Запоминаем, кто проверял
        }
    }

    void handleReviewAssignment(const Message& msg) {
        if (msg.client_id != client_id)
            return;

        if (msg.program_id == 0) {
            // Нет программ для проверки
            if (current_state == WAITING_REVIEW) {
                current_state = SLEEPING;
                std::cout << "😴 Нет программ для проверки. Засыпаю..." << std::endl;
            }
        } else {
            // Есть программа для проверки
            std::cout << "📝 Получил программу '" << msg.data << "' (ID: " << msg.program_id
                      << ") от программиста " << msg.target_id << " для проверки" << std::endl;

            current_state = REVIEWING;

            // Имитируем проверку
            std::this_thread::sleep_for(std::chrono::seconds(3 + gen() % 5));

            // Случайный результат проверки (70% правильно, 30% неправильно)
            ReviewResult result = (gen() % 100 < 70) ? CORRECT : INCORRECT;

            // Отправляем результат
            Message result_msg;
            result_msg.type = REVIEW_RESULT;
            result_msg.client_id = client_id;
            result_msg.target_id = msg.target_id;
            result_msg.program_id = msg.program_id;
            result_msg.result = result;
            strcpy(result_msg.data,
                   (result == CORRECT) ? "Program is correct" : "Program has errors");

            NetworkUtils::sendMessage(sockfd, result_msg, server_ip, server_port);

            programs_reviewed++;
            std::cout << "✅ Проверил программу " << msg.program_id
                      << " - результат: " << (result == CORRECT ? "ПРАВИЛЬНО" : "НЕПРАВИЛЬНО")
                      << std::endl;

            current_state = WRITING;
        }
    }

    void handleAssignmentNotification(const Message& msg) {
        if (msg.client_id != client_id)
            return;

        std::cout << "🔔 Получено уведомление о новой программе для проверки: '" << msg.data
                  << "' (ID: " << msg.program_id << ")" << std::endl;
    }

    void handleShutdown(const Message& msg) {
        std::cout << "🛑 Получена команда завершения от сервера: " << msg.data << std::endl;
        running = false;
    }

    void performWork() {
        if (!registered || !running)
            return;

        switch (current_state) {
            case WRITING:
                writeProgram();
                break;
            case WAITING_REVIEW:
                requestReview();
                break;
            case FIXING:
                fixProgram();
                break;
            case SLEEPING:
                requestReview();  // Периодически проверяем, не появились ли программы для проверки
                break;
            default:
                break;
        }
    }

    void writeProgram() {
        // Имитируем написание программы
        std::cout << "💻 Пишу программу..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5 + gen() % 10));

        current_program_id++;
        std::string program_name =
            "Программа_" + std::to_string(current_program_id) + "_от_" + programmer_name;

        // Выбираем случайного программиста для проверки (исключая себя)
        std::vector<int> available_reviewers;
        for (int i = 1; i <= 10; i++) {  // Предполагаем максимум 10 программистов
            if (i != client_id) {
                available_reviewers.push_back(i);
            }
        }

        if (available_reviewers.empty()) {
            std::cout << "❌ Нет доступных программистов для проверки" << std::endl;
            return;
        }

        int target_id = available_reviewers[gen() % available_reviewers.size()];

        // Отправляем программу на проверку
        Message msg;
        msg.type = SUBMIT_PROGRAM;
        msg.client_id = client_id;
        msg.target_id = target_id;
        msg.program_id = current_program_id;
        strcpy(msg.data, program_name.c_str());

        if (NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port)) {
            std::cout << "📤 Отправил программу '" << program_name << "' на проверку программисту "
                      << target_id << std::endl;
            current_state = WAITING_REVIEW;
        } else {
            std::cout << "❌ Ошибка отправки программы на сервер" << std::endl;
        }
    }

    void fixProgram() {
        std::cout << "🔧 Исправляю программу " << current_program_id << "..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3 + gen() % 5));

        // Отправляем исправленную программу тому же проверяющему
        std::string program_name = "Исправленная_программа_" + std::to_string(current_program_id) +
                                   "_от_" + programmer_name;

        Message msg;
        msg.type = SUBMIT_PROGRAM;
        msg.client_id = client_id;
        msg.target_id = review_target_id;
        msg.program_id = current_program_id;
        strcpy(msg.data, program_name.c_str());

        if (NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port)) {
            std::cout << "📤 Отправил исправленную программу '" << program_name
                      << "' на повторную проверку программисту " << review_target_id << std::endl;
            current_state = WAITING_REVIEW;
        } else {
            std::cout << "❌ Ошибка отправки исправленной программы на сервер" << std::endl;
        }
    }

    void requestReview() {
        // Запрашиваем программу для проверки
        Message msg;
        msg.type = REQUEST_REVIEW;
        msg.client_id = client_id;
        strcpy(msg.data, "Requesting program to review");

        NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port);
    }

    void sendHeartbeat() {
        if (!registered)
            return;

        Message msg;
        msg.type = HEARTBEAT;
        msg.client_id = client_id;
        strcpy(msg.data, "alive");

        NetworkUtils::sendMessage(sockfd, msg, server_ip, server_port);
    }

    void printStatus() {
        std::cout << "\n=== СТАТУС ПРОГРАММИСТА '" << programmer_name << "' ===" << std::endl;
        std::cout << "ID: " << client_id << std::endl;
        std::cout << "Состояние: ";

        switch (current_state) {
            case WRITING:
                std::cout << "Пишет программу";
                break;
            case WAITING_REVIEW:
                std::cout << "Ожидает проверки";
                break;
            case REVIEWING:
                std::cout << "Проверяет программу";
                break;
            case FIXING:
                std::cout << "Исправляет программу";
                break;
            case SLEEPING:
                std::cout << "Спит";
                break;
        }

        std::cout << std::endl;
        std::cout << "Программ написано: " << programs_written << std::endl;
        std::cout << "Программ проверено: " << programs_reviewed << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
};

ProgrammerClient* ProgrammerClient::instance = nullptr;

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cout << "Использование: " << argv[0]
                  << " <ИМЯ> <SERVER_IP> <SERVER_PORT> <CLIENT_PORT>" << std::endl;
        std::cout << "Пример: " << argv[0] << " Иван 127.0.0.1 8080 8081" << std::endl;
        return 1;
    }

    std::string programmer_name = argv[1];
    std::string server_ip = argv[2];
    int server_port = std::atoi(argv[3]);
    int client_port = std::atoi(argv[4]);

    if (server_port <= 0 || server_port > 65535 || client_port <= 0 || client_port > 65535) {
        std::cout << "Ошибка: некорректный порт" << std::endl;
        return 1;
    }

    ProgrammerClient client(programmer_name, server_ip, server_port, client_port);

    if (!client.start()) {
        std::cout << "Ошибка запуска клиента" << std::endl;
        return 1;
    }

    return 0;
}
