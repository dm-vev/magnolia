SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export srctree="$SCRIPT_DIR"
export IDF_PROJECT_PATH="$SCRIPT_DIR"
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.qemu" idf.py -B build/qemu qemu monitor
