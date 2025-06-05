#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>
#include <getopt.h>
#include <pwd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "libmysyslog.h"

#define BUF_SIZE 4096
#define TIMEOUT_SEC 5

void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  -c, --command \"bash_command\"    Bash command to execute\n");
    printf("  -h, --host \"ip_addr\"             Server IP address\n");
    printf("  -p, --port PORT                  Server port\n");
    printf("  -s, --stream                     Use stream (TCP) socket\n");
    printf("  -d, --dgram                      Use datagram (UDP) socket\n");
    printf("      --help                       Show this help message\n");
}

int main(int argc, char *argv[]) {
    char *command = NULL, *host = NULL;
    int port = 0, use_stream = 0, use_dgram = 0;

    static struct option long_opts[] = {
        {"command", required_argument, 0, 'c'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"stream", no_argument, 0, 's'},
        {"dgram", no_argument, 0, 'd'},
        {"help", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    // Парсинг аргументов
    int opt, opt_index;
    while ((opt = getopt_long(argc, argv, "c:h:p:sd", long_opts, &opt_index)) != -1) {
        switch (opt) {
            case 'c': command = strdup(optarg); break;
            case 'h': host = strdup(optarg); break;
            case 'p': port = atoi(optarg); break;
            case 's': use_stream = 1; break;
            case 'd': use_dgram = 1; break;
            case 0:
                if (strcmp(long_opts[opt_index].name, "help") == 0) {
                    print_usage(argv[0]);
                    return 0;
                }
                break;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (!command || !host || port <= 0 || (!use_stream && !use_dgram)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Получение имени пользователя
    struct passwd *pw = getpwuid(getuid());
    if (!pw) {
        log_error("Ошибка получения имени пользователя: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    const char *username = pw->pw_name;

    // Формирование JSON-запроса
    struct json_object *req = json_object_new_object();
    if (!req) {
        log_error("Не удалось создать JSON-объект");
        return EXIT_FAILURE;
    }

    json_object_object_add(req, "login", json_object_new_string(username));
    json_object_object_add(req, "command", json_object_new_string(command));
    const char *json_str = json_object_to_json_string(req);

    log_info("Формируется запрос от пользователя '%s': %s", username, json_str);

    // Создание сокета
    int sockfd = socket(AF_INET, use_stream ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0) {
        log_error("Ошибка создания сокета: %s", strerror(errno));
        json_object_put(req);
        return EXIT_FAILURE;
    }

    // Настройка адреса сервера
    struct sockaddr_in servaddr = {0};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &servaddr.sin_addr) != 1) {
        log_error("Неверный IP-адрес: %s", host);
        close(sockfd);
        json_object_put(req);
        return EXIT_FAILURE;
    }

    // TCP или UDP обработка
    if (use_stream) {
        if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            log_error("Ошибка подключения к серверу: %s", strerror(errno));
            close(sockfd);
            json_object_put(req);
            return EXIT_FAILURE;
        }

        if (send(sockfd, json_str, strlen(json_str), 0) < 0) {
            log_error("Ошибка отправки TCP-запроса: %s", strerror(errno));
            close(sockfd);
            json_object_put(req);
            return EXIT_FAILURE;
        }
char buf[BUF_SIZE] = {0};
        ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            printf("Ответ сервера:\n%s\n", buf);
            log_info("Ответ TCP получен: %s", buf);
        } else {
            log_warning("Нет ответа от сервера: %s", strerror(errno));
        }

    } else {
        // UDP: установка таймаута
        struct timeval tv = {TIMEOUT_SEC, 0};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (sendto(sockfd, json_str, strlen(json_str), 0,
                   (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            log_error("Ошибка отправки UDP-запроса: %s", strerror(errno));
            close(sockfd);
            json_object_put(req);
            return EXIT_FAILURE;
        }

        char buf[BUF_SIZE] = {0};
        socklen_t len = sizeof(servaddr);
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr*)&servaddr, &len);

        if (n > 0) {
            buf[n] = '\0';
            printf("Ответ сервера:\n%s\n", buf);
            log_info("Ответ UDP получен: %s", buf);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                log_warning("Таймаут ожидания ответа от сервера");
                fprintf(stderr, "UDP: timeout\n");
            } else {
                log_error("Ошибка при получении ответа: %s", strerror(errno));
            }
        }
    }

    close(sockfd);
    json_object_put(req);
    free(command);
    free(host);

    return 0;
}
