PRESET ?= mingw-debug
BUILD_DIR ?= build-debug
EXE := $(BUILD_DIR)/vulkan-starter-app.exe

.PHONY: all configure build run clean rebuild

all: build

configure:
	cmake --preset $(PRESET)

build: configure
	cmake --build $(BUILD_DIR) --parallel

run: build
	./$(EXE)

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean build
