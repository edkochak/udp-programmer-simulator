# Makefile for Programmers UDP Client-Server Application (10 balls)

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -pthread -O2
INCLUDES = -I./common

# Директории
SERVER_DIR = server
PROGRAMMER_DIR = programmer_client
OBSERVER_DIR = observer_client
BUILD_DIR = build

# Исполняемые файлы
SERVER_BIN = $(BUILD_DIR)/server
PROGRAMMER_BIN = $(BUILD_DIR)/programmer
OBSERVER_BIN = $(BUILD_DIR)/observer

# Исходные файлы
SERVER_SRC = $(SERVER_DIR)/server.cpp
PROGRAMMER_SRC = $(PROGRAMMER_DIR)/programmer.cpp
OBSERVER_SRC = $(OBSERVER_DIR)/observer.cpp

.PHONY: all clean server programmer observer run-demo help

all: $(BUILD_DIR) $(SERVER_BIN) $(PROGRAMMER_BIN) $(OBSERVER_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SERVER_BIN): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

$(PROGRAMMER_BIN): $(PROGRAMMER_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

$(OBSERVER_BIN): $(OBSERVER_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

server: $(SERVER_BIN)

programmer: $(PROGRAMMER_BIN)

observer: $(OBSERVER_BIN)

clean:
	rm -rf $(BUILD_DIR)

# Демонстрационный запуск
run-demo: all
	@echo "=== ДЕМОНСТРАЦИЯ РАБОТЫ СИСТЕМЫ ==="
	@echo "1. Запускается сервер на порту 8080"
	@echo "2. Через 2 секунды запускаются 3 программиста"
	@echo "3. Через 5 секунд запускается наблюдатель"
	@echo "4. Для остановки используйте Ctrl+C"
	@echo
	@echo "Запуск сервера..."
	@$(SERVER_BIN) 127.0.0.1 8080 &
	@sleep 2
	@echo "Запуск программистов..."
	@$(PROGRAMMER_BIN) "Иван" 127.0.0.1 8080 8081 &
	@$(PROGRAMMER_BIN) "Петр" 127.0.0.1 8080 8082 &
	@$(PROGRAMMER_BIN) "Мария" 127.0.0.1 8080 8083 &
	@sleep 3
	@echo "Запуск наблюдателя..."
	@$(OBSERVER_BIN) 127.0.0.1 8080 8090

# Ручной запуск компонентов
run-server: $(SERVER_BIN)
	@echo "Запуск сервера на 127.0.0.1:8080"
	@echo "Для остановки нажмите Ctrl+C"
	@$(SERVER_BIN) 127.0.0.1 8080

run-programmer1: $(PROGRAMMER_BIN)
	@$(PROGRAMMER_BIN) "Иван" 127.0.0.1 8080 8081

run-programmer2: $(PROGRAMMER_BIN)
	@$(PROGRAMMER_BIN) "Петр" 127.0.0.1 8080 8082

run-programmer3: $(PROGRAMMER_BIN)
	@$(PROGRAMMER_BIN) "Мария" 127.0.0.1 8080 8083

run-observer: $(OBSERVER_BIN)
	@$(OBSERVER_BIN) 127.0.0.1 8080 8090

help:
	@echo "Доступные команды:"
	@echo "  make all         - собрать все компоненты"
	@echo "  make server      - собрать только сервер"
	@echo "  make programmer  - собрать только клиент-программист"
	@echo "  make observer    - собрать только клиент-наблюдатель"
	@echo "  make clean       - очистить собранные файлы"
	@echo ""
	@echo "Запуск демонстрации:"
	@echo "  make run-demo    - автоматический запуск всей системы"
	@echo ""
	@echo "Ручной запуск компонентов:"
	@echo "  make run-server     - запустить сервер"
	@echo "  make run-programmer1 - запустить программиста 'Иван'"
	@echo "  make run-programmer2 - запустить программиста 'Петр'"
	@echo "  make run-programmer3 - запустить программиста 'Мария'"
	@echo "  make run-observer   - запустить наблюдателя"
	@echo ""
	@echo "Параметры командной строки:"
	@echo "  Сервер: ./server <IP> <PORT>"
	@echo "  Программист: ./programmer <ИМЯ> <SERVER_IP> <SERVER_PORT> <CLIENT_PORT>"
	@echo "  Наблюдатель: ./observer <SERVER_IP> <SERVER_PORT> <CLIENT_PORT>"
