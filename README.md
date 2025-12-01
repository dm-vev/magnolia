# MagnoliaOS (ESP32-S3)

- Монолитное приложение ESP-IDF, построенное вокруг unix-каркаса xv6 (в `main/kernel/esp32/`) с минимальным количеством низкоуровневого кода:
- файловая система xv6 заменена на ESP-IDF VFS (FAT на SPI flash);
- планировщик — FreeRTOS, задачи ядра поднимаются как FreeRTOS tasks;
- загрузчик ELF убран: собирается один образ, внешний загрузчик можно интегрировать позже.
- файловая система xv6 заменена на ESP-IDF VFS (FAT на SPI flash);
- планировщик — FreeRTOS, задачи ядра поднимаются как FreeRTOS tasks;
- загрузчик ELF убран: собирается один образ, внешний загрузчик можно интегрировать позже.

## Как собрать и запустить

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

## Что внутри

- `main/kernel/esp32/` — unix-каркас: FreeRTOS-задачи, монтирование ESP-IDF VFS и лог (без ассемблера и без trap/VM/PLIC);
- `main/kernel/esp32/xv6_shim.c` — демонстрационный init-процесс: пишет boot log в `MAGNOLIA_FS_BASE_PATH/boot.log` через POSIX-вызовы;
- `main/main.c` — точка входа ESP-IDF, вызывает `kernel_boot()`.
## Настройка

`idf.py menuconfig` → `MagnoliaOS Configuration`:
- `MAGNOLIA_FS_BASE_PATH` — точка монтирования VFS (по умолчанию `/fs`);
- `MAGNOLIA_FS_PARTITION_LABEL` — метка раздела в таблице разделов (по умолчанию `storage`);
- лимиты стека и приоритеты задач `xv6-init` и heartbeat.
