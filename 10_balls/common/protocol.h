#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstring>
#include <ctime>
#include <string>

// Типы сообщений
enum MessageType {
    REGISTER_PROGRAMMER = 1,
    REGISTER_OBSERVER = 2,
    SUBMIT_PROGRAM = 3,
    REQUEST_REVIEW = 4,
    REVIEW_RESULT = 5,
    STATUS_UPDATE = 6,
    DISCONNECT = 7,
    SHUTDOWN = 8,
    HEARTBEAT = 9,
    ASSIGNMENT_NOTIFICATION = 10
};

// Статусы программиста
enum ProgrammerState { WRITING = 1, WAITING_REVIEW = 2, REVIEWING = 3, FIXING = 4, SLEEPING = 5 };

// Результаты проверки
enum ReviewResult { CORRECT = 1, INCORRECT = 2 };

// Базовая структура сообщения
struct Message {
    MessageType type;
    int client_id;
    int target_id;
    int program_id;
    int reviewer_id;
    ReviewResult result;
    ProgrammerState state;
    char data[256];
    time_t timestamp;

    Message()
        : type(HEARTBEAT),
          client_id(0),
          target_id(0),
          program_id(0),
          reviewer_id(0),
          result(CORRECT),
          state(WRITING),
          timestamp(time(nullptr)) {
        memset(data, 0, sizeof(data));
    }
};

// Структура для программы на проверку
struct ProgramReview {
    int program_id;
    int author_id;
    int reviewer_id;
    std::string program_name;
    time_t submitted_time;

    ProgramReview(int pid, int aid, int rid, const std::string& name)
        : program_id(pid),
          author_id(aid),
          reviewer_id(rid),
          program_name(name),
          submitted_time(time(nullptr)) {}
};

// Информация о программисте
struct ProgrammerInfo {
    int id;
    std::string name;
    ProgrammerState state;
    int programs_written;
    int programs_reviewed;
    int current_program_id;
    std::string current_activity;
    time_t last_activity;
    bool is_connected;

    ProgrammerInfo()
        : id(0),
          name(""),
          state(WRITING),
          programs_written(0),
          programs_reviewed(0),
          current_program_id(0),
          current_activity(""),
          last_activity(time(nullptr)),
          is_connected(false) {}

    ProgrammerInfo(int pid, const std::string& pname)
        : id(pid),
          name(pname),
          state(WRITING),
          programs_written(0),
          programs_reviewed(0),
          current_program_id(0),
          current_activity("Starting work"),
          last_activity(time(nullptr)),
          is_connected(true) {}
};

// Константы
const int MAX_PROGRAMMERS = 10;
const int HEARTBEAT_INTERVAL = 5;  // секунд
const int CLIENT_TIMEOUT = 15;     // секунд
const int BUFFER_SIZE = 512;

#endif  // PROTOCOL_H
