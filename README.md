# Интерфейс RV-C для зарядных устройств с CAN или RS485

## Сборка

``` shell
python -m venv .venv
.venv/bin/pip install -r requirements.txt
make menuconfig
BUILD_DIR=... make # по умолчанию build
```

Выходные файлы firwmare.elf, firmware.bin, output.map

