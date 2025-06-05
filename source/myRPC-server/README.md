# Server

myRPC-server — это демон, прослушивающий входящие TCP или UDP соединения (в зависимости от конфигурации).

Запрос:
   ```bash
   { "login": "admin", "command": "whoami /n" }
   ```

Ответ:
   ```bash
   Ответ от сервера:
{ "code": 0,  "result": "root /n" }

   ```
