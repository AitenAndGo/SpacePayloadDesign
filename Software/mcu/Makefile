# ============================
# Raspberry Pi Pico Project
# ============================

BUILD_DIR = build

all:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ..
	cd $(BUILD_DIR) && make

clean:
	rm -rf $(BUILD_DIR)

flash:
	cd $(BUILD_DIR) && make flash