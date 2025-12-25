PY ?= python3
PORT ?=
BAUD ?=
IDF_EXPORT ?= esp-idf/export.sh

.PHONY: applets vfs-image flash-vfs flash
.PHONY: qemu

applets vfs-image:
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && $(PY) tools/applets.py build'

flash-vfs:
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && $(PY) tools/applets.py flash $(if $(PORT),--port $(PORT),) $(if $(BAUD),--baud $(BAUD),)'

flash:
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && $(PY) tools/applets.py build'
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && idf.py $(if $(PORT),-p $(PORT),) flash'
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && $(PY) tools/applets.py flash $(if $(PORT),--port $(PORT),) $(if $(BAUD),--baud $(BAUD),)'

qemu:
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && idf.py build'
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && $(PY) tools/applets.py build'
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1 && $(PY) tools/applets.py qemu-image'
	bash -lc 'source $(IDF_EXPORT) >/dev/null 2>&1; set -e; qemu-system-xtensa -M esp32s3 -drive file=build/qemu_flash.bin,if=mtd,format=raw -drive file=build/qemu_efuse.bin,if=none,format=raw,id=efuse -global driver=nvram.esp32c3.efuse,property=drive,value=efuse -global driver=timer.esp32s3.timg,property=wdt_disable,value=true -nic user,model=open_eth -nographic -serial tcp::5555,server & QPID=$$!; trap "kill $$QPID 2>/dev/null || true" EXIT; sleep 1; idf.py monitor -p socket://localhost:5555'
