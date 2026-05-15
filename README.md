# Accelerometer distributed processing system

Проект реализует распределённую систему обработки данных акселерометра на C++ с использованием gRPC и Protocol Buffers.

## Архитектура

В системе участвуют три процесса:

1. **Node A / sender** (`accel_client_a`)
   - генерирует/считывает данные акселерометра с частотой около 50 Гц;
   - отправляет `AccelPacket` на сервер;
   - получает `AccelModule` от сервера;
   - пишет результат в `accel_module.log`.

2. **Server** (`accel_server`)
   - принимает поток данных от Node A;
   - отбрасывает подряд идущие дубликаты `x`, `y`, `z` с точностью `1e-4`;
   - пересылает валидные пакеты Node B;
   - принимает рассчитанный модуль от Node B;
   - пересылает модуль обратно Node A.

3. **Node B / processor** (`accel_client_b`)
   - получает от сервера валидированные `AccelPacket`;
   - вычисляет `sqrt(x*x + y*y + z*z)`;
   - отправляет `AccelModule` обратно на сервер.

## gRPC API

Файл протокола: `proto/sensor_data.proto`.

Используется один bidirectional streaming RPC:

- `StreamAccelData(stream StreamMessage) returns (stream StreamMessage)`.

Единый поток используется и Node A, и Node B. Первое сообщение каждого клиента — `ClientHello`, где указывается роль:

- `CLIENT_ROLE_SENDER` — Node A;
- `CLIENT_ROLE_PROCESSOR` — Node B.

После регистрации роли сервер маршрутизирует сообщения по `oneof payload` внутри `StreamMessage`:

- `AccelPacket` от Node A пересылается Node B;
- `AccelModule` от Node B пересылается Node A.

Такой wrapper выбран для строгой поддержки одного RPC из ТЗ и одновременно для корректной передачи разных прикладных типов в обоих направлениях.

В каждое прикладное сообщение добавлено поле `version`. Текущая поддерживаемая версия протокола — `1`. Сервер игнорирует сообщения с неподдерживаемой версией.

## Асинхронность и переподключение

Серверная часть реализована через gRPC C++ callback API: `AccelerometerService::CallbackService` и `grpc::ServerBidiReactor`.

Клиенты используют bidirectional streaming и автоматически переподключаются при разрыве соединения. Задержка переподключения растёт экспоненциально от `500 ms` до `30 s`.

## Сборка Linux-версии

Требования:

- CMake >= 3.16;
- C++17 compiler;
- Protobuf;
- gRPC C++.

На macOS/Homebrew пример установки зависимостей:

```bash
brew install cmake protobuf grpc
```

На Ubuntu зависимости можно поставить из пакетов или собрать gRPC из исходников. Для Ubuntu 22.04 часто удобнее использовать vcpkg или сборку gRPC по официальной инструкции.

Сборка:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target accel_server accel_client_a accel_client_b -j 4
```

## CI

Добавлен GitHub Actions workflow: `.github/workflows/ci.yml`.

CI выполняет на `ubuntu-24.04`:

1. установку CMake, Ninja, Protobuf, gRPC и OpenSSL;
2. конфигурацию проекта через CMake;
3. сборку сервера, клиентов и тестов;
4. запуск `ctest --output-on-failure`.

Workflow запускается на `push`, `pull_request` и вручную через `workflow_dispatch`.

## Тесты

Тесты написаны на GoogleTest и подключаются через CMake `FetchContent`.

Сборка с тестами:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --target accel_unit_tests accel_model_tests accel_tls_tests -j 4
```

Запуск тестов:

```bash
ctest --test-dir build --output-on-failure
```

Состав тестов:

- `accel_unit_tests` — unit-тесты `DuplicateFilter`, `ModuleLogger`, расчёта модуля ускорения;
- `accel_model_tests` — модельный end-to-end тест: поднимает gRPC-сервер, запускает Node B и Node A, проверяет фильтрацию дубликата и возврат рассчитанных модулей в лог Node A;
- `accel_tls_tests` — TLS/mTLS model-тесты: проверяют end-to-end pipeline поверх mTLS с API-key и отказ при неверном API-key.

## Запуск

Терминал 1 — сервер:

```bash
./build/accel_server 0.0.0.0:50051
```

Терминал 2 — Node B:

```bash
./build/accel_client_b 127.0.0.1:50051
```

Терминал 3 — Node A:

```bash
./build/accel_client_a 127.0.0.1:50051 accel_module.log
```

После запуска Node A будет писать рассчитанные значения в файл:

```text
<timestamp_ms> <module>
```

## Проверенная команда локального smoke-test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --target accel_server accel_client_a accel_client_b accel_unit_tests accel_model_tests accel_tls_tests -j 4
ctest --test-dir build --output-on-failure
```

После этого можно запустить три процесса по инструкции выше.

## Формат данных

`AccelPacket`:

- `version` — версия протокола;
- `timestamp` — миллисекунды с начала Unix epoch;
- `x`, `y`, `z` — компоненты ускорения.

`AccelModule`:

- `version` — версия протокола;
- `timestamp` — timestamp исходного пакета;
- `module` — модуль вектора ускорения.

## Фильтрация дубликатов

Дубликат — это пакет, у которого `x`, `y`, `z` равны предыдущему принятому пакету с точностью `1e-4`:

```cpp
fabs(last.x - current.x) <= 1e-4 &&
fabs(last.y - current.y) <= 1e-4 &&
fabs(last.z - current.z) <= 1e-4
```

Фильтрация выполняется только на сервере.

## Обоснование Protobuf/gRPC

Для уровня 2 выбран Protocol Buffers и gRPC, потому что:

- Protobuf компактнее JSON и быстрее сериализуется;
- схема сообщений строго типизирована;
- gRPC напрямую поддерживает bidirectional streaming;
- удобно добавлять новые поля без поломки старых клиентов при соблюдении правил совместимости.

Сравнение с альтернативами:

- **JSON** проще для отладки, но больше по размеру и медленнее парсится;
- **MessagePack/CBOR** компактнее JSON, но не дают такой строгой схемы API как Protobuf;
- **Cap'n Proto** очень быстрый, но менее распространён в Android/gRPC-экосистеме;
- **FlatBuffers** хорош для zero-copy сценариев, но gRPC + Protobuf проще интегрировать в это задание.

## Android/NDK

В проекте оставлены заготовки для Android-части:

- `client_a/android_sensor_source.cpp`;
- `client_a/jni_bridge.cpp`;
- `client_b/jni_bridge.cpp`;
- Android-приложение в `app/`.

Текущая проверенная сборка — Linux-вариант с эмуляцией датчика в `client_a/main.cpp`.

Для полноценной Android-интеграции нужно:

1. собрать gRPC и Protobuf под Android NDK;
2. подключить native-библиотеки через Gradle/CMake;
3. запускать C++ код из минимальной Java/Kotlin-обёртки;
4. писать лог в каталог приложения, например `Context.getExternalFilesDir(null)`.

Если запускать native executable напрямую через `adb shell`, возможны ограничения SELinux. Рекомендуемый способ — минимальная Android-обёртка с корректным lifecycle и разрешениями.

## TLS и API key

Уровень 3 реализован для Linux-варианта.

Скрипт генерации сертификатов:

```bash
./scripts/generate_certs.sh certs
```

Скрипт создаёт:

- `certs/ca.crt`, `certs/ca.key`;
- `certs/server.crt`, `certs/server.key`;
- `certs/client.crt`, `certs/client.key`.

Сервер поддерживает:

- обычный insecure-режим по умолчанию;
- one-way TLS через `--tls`;
- mTLS через `--mtls`;
- API-key через metadata `x-api-key`.

Пример запуска с mTLS и API-key:

```bash
./build/accel_server 0.0.0.0:50051 --mtls certs/ca.crt certs/server.crt certs/server.key --api-key test-api-key
```

Node B:

```bash
./build/accel_client_b 127.0.0.1:50051 --tls certs/ca.crt certs/client.crt certs/client.key --api-key test-api-key
```

Node A:

```bash
./build/accel_client_a 127.0.0.1:50051 accel_module.log --tls certs/ca.crt certs/client.crt certs/client.key --api-key test-api-key
```

TLS/API-key проверяется тестом `accel_tls_tests`:

- успешный end-to-end обмен поверх mTLS;
- отказ сервера при неверном API-key поверх TLS.
