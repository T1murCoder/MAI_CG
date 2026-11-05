ifeq ($(OS),Windows_NT)
    PRESET ?= mingw-debug
    BUILD_DIR ?= build-debug
    EXE_SUFFIX := .exe
else
    PRESET ?= debug
    BUILD_DIR ?= build-debug
    EXE_SUFFIX :=
endif

EXE := $(BUILD_DIR)/vulkan-starter-app$(EXE_SUFFIX)

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
