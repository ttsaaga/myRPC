#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <json-c/json.h>
#include <signal.h>
#include <sys/stat.h>
#include "libmysyslog.h"

#define MAX_BUFFER 4096
#define CONF_PATH "/etc/myRPC/myRPC.conf"
#define USERS_PATH "/etc/myRPC/users.conf"

static inline double now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int is_user_authorized(const char *username) {
    FILE *fp = fopen(USERS_PATH, "r");
    if (!fp) {
        log_error("Не удалось открыть %s: %s", USERS_PATH, strerror(errno));
        return 0;
    }

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, username) == 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

static char *read_text_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return strdup("(не удалось открыть файл)");

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *content = malloc(size + 1);
    if (!content) {
        fclose(fp);
        return strdup("(ошибка выделения памяти)");
    }

    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);
    return content;
}

static char *escape_for_shell(const char *input) {
    size_t len = strlen(input);
    char *buf = malloc(len * 4 + 3);
    if (!buf) return NULL;

    char *p = buf;
    *p++ = '\'';
    for (size_t i = 0; i < len; ++i) {
        if (input[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = input[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return buf;
}

static void process_request(const char *input, char *output_json) {
    struct json_object *parsed = json_tokener_parse(input);
    struct json_object *resp = json_object_new_object();

    if (!parsed || !json_object_is_type(parsed, json_type_object)) {
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Невалидный JSON"));
        goto done;
    }

    struct json_object *jlogin, *jcmd;
    if (!json_object_object_get_ex(parsed, "login", &jlogin) ||
        !json_object_object_get_ex(parsed, "command", &jcmd) ||
        !json_object_is_type(jlogin, json_type_string) ||
        !json_object_is_type(jcmd, json_type_string)) {

        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Отсутствуют поля 'login' или 'command'"));
        goto done;
    }

    const char *login = json_object_get_string(jlogin);
    const char *command = json_object_get_string(jcmd);

    if (!is_user_authorized(login)) {
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Пользователь не авторизован"));
        goto done;
    }

    char tmp_template[] = "/tmp/myRPC_XXXXXX";
    int fd = mkstemp(tmp_template);
    if (fd < 0) {
        log_error("mkstemp: %s", strerror(errno));
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Не удалось создать временный файл"));
        goto done;
    }
    close(fd);

    char out_path[256], err_path[256];
    snprintf(out_path, sizeof(out_path), "%s.stdout", tmp_template);
    snprintf(err_path, sizeof(err_path), "%s.stderr", tmp_template);

    char *escaped = escape_for_shell(command);
    if (!escaped) {
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Ошибка экранирования команды"));
        goto done;
    }
char sh_cmd[1024];
    snprintf(sh_cmd, sizeof(sh_cmd), "sh -c %s > %s 2> %s", escaped, out_path, err_path);
    free(escaped);

    double start = now_ms();
    int rc = system(sh_cmd);
    double duration = now_ms() - start;

    char *result = read_text_file(rc == 0 ? out_path : err_path);

    json_object_object_add(resp, "code", json_object_new_int(rc == 0 ? 0 : 1));
    json_object_object_add(resp, "duration_ms", json_object_new_double(duration));
    json_object_object_add(resp, "result", json_object_new_string(result));
    free(result);

done:
    strncpy(output_json, json_object_to_json_string(resp), MAX_BUFFER - 1);
    output_json[MAX_BUFFER - 1] = '\0';
    json_object_put(resp);
    if (parsed) json_object_put(parsed);
}

static void load_config(int *port, int *is_stream) {
    FILE *fp = fopen(CONF_PATH, "r");
    if (!fp) {
        log_warning("Не удалось открыть %s: %s", CONF_PATH, strerror(errno));
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (sscanf(line, "port = %d", port) == 1) continue;
        if (strstr(line, "socket_type = dgram")) *is_stream = 0;
        if (strstr(line, "socket_type = stream")) *is_stream = 1;
    }

    fclose(fp);
}

int main() {
    log_info("Запуск демона myRPC-server");

    int port = 1234;
    int use_stream = 1;
    load_config(&port, &use_stream);
    log_info("Конфигурация: порт=%d, тип сокета=%s", port, use_stream ? "TCP" : "UDP");

    int sockfd = socket(AF_INET, use_stream ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0) {
        log_error("socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_error("bind: %s", strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (use_stream && listen(sockfd, 5) < 0) {
        log_error("listen: %s", strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    log_info("Сервер готов принимать подключения");

    while (1) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        char buffer[MAX_BUFFER] = {0};

        if (use_stream) {
            int csock = accept(sockfd, (struct sockaddr*)&client, &len);
            if (csock < 0) continue;

            ssize_t n = recv(csock, buffer, sizeof(buffer) - 1, 0);
            buffer[n > 0 ? n : 0] = '\0';

            log_info("TCP-запрос от %s: %s", inet_ntoa(client.sin_addr), buffer);

            char response[MAX_BUFFER];
            process_request(buffer, response);
            send(csock, response, strlen(response), 0);
            close(csock);
        } else {
            ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                                 (struct sockaddr*)&client, &len);
            buffer[n > 0 ? n : 0] = '\0';

            log_info("UDP-запрос от %s: %s", inet_ntoa(client.sin_addr), buffer);

            char response[MAX_BUFFER];
            process_request(buffer, response);
            sendto(sockfd, response, strlen(response), 0, (struct sockaddr*)&client, len);
        }
    }

    close(sockfd);
    return 0;
}
