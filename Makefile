PY ?= python3

.PHONY: applets vfs-image flash-vfs flash

applets vfs-image:
	$(PY) tools/applets.py build

flash-vfs:
	$(PY) tools/applets.py flash

flash:
	idf.py flash
