# myRPC

---

## Сборка проекта

Для сборки проекта используйте Makefile

Введите:

1. Клонируйте репозиторий:
   ```bash
   git clone <ссылка на этот проект>
   ```
2. Перейдите в директорию с проектом:
   ```bash
    cd ~/myRPC/
   ```
3. Соберите проект:
   ```bash
    make all
   ```
4. Соберите .deb пакеты:
   ```bash
    make deb
   ```
5. Создайте репозиторий .deb пакетов:
   ```bash
    make repo
   ```

## myRPC-server

Он должен автоматически установиться при скачивании .deb пакета.
Если этого не произошло значит попробуйте вручную.

Узнаем статус демона:
   ```bash
   systemctl status myRPC-server
   ```
Перезапускаем демоны:
   ```bash
   systemctl daemon-reload
   ```
Перезапускаем сервер:
   ```bash
   systemctl restart myRPC-server
   ```

## myRPC-client

С клиентом все куда проще.
Его можно запускать через папку bin в myRPC или если вы установили .deb пакет значит myRPC-client. 
Запуск через /bin/myRPC-client:
   ```bash
   ./bin/myRPC-client <ключи>
   ```
Ключи:

-c , команда
-h , айпи
-p , порт
-d dgram / -s stream сокет

Пример рабочей команды:
   ```bash
   myRPC-client -c "whoami" -h 127.0.0.1 -p 4040 -d
   ```
