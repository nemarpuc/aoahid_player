# Convenience wrapper around the CMake build documented in README.md's
# "Building from source" section, for Linux/macOS shells. Windows uses the
# Visual Studio generator directly (see CMakePresets.json's windows-release
# preset) and has no `make` by default, so this is not meant for it.
#
# `AOAHID_PREFIX` points CMAKE_PREFIX_PATH at libaoahid (see README.md's
# "Getting libaoahid"); leave it unset if it is already findable (installed
# system-wide, or already on CMAKE_PREFIX_PATH in the environment).
#
#   make AOAHID_PREFIX=/path/to/libaoahid-0.5.3-linux-x86_64-...   # first configure
#   make                                                            # rebuild after
#   make test
#   make run
#   make clean

BUILD_DIR  ?= build
AOAHID_PREFIX ?=
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

CMAKE_CONFIGURE = cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
ifneq ($(strip $(AOAHID_PREFIX)),)
CMAKE_CONFIGURE += -DCMAKE_PREFIX_PATH=$(AOAHID_PREFIX)
endif

GUI_BIN := $(BUILD_DIR)/out/Release/aoahid_player_gui

.PHONY: all build configure test run clean help

all: build

help:
	@echo "Targets:"
	@echo "  build      Configure (if needed) and build everything [default]"
	@echo "  test       Build, then run the unit tests (ctest)"
	@echo "  run        Build, then launch the GUI"
	@echo "  clean      Remove the build directory ($(BUILD_DIR))"
	@echo "Variables:"
	@echo "  AOAHID_PREFIX=<dir>  CMAKE_PREFIX_PATH for libaoahid (first configure only)"
	@echo "  BUILD_DIR=<dir>      Build tree location (default: build)"

# Re-runs only when the build tree has never been configured; CMake itself
# notices later CMakeLists.txt/CMakePresets.json changes and reconfigures
# during `cmake --build`, so this does not need to track those.
$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE_CONFIGURE)

configure: $(BUILD_DIR)/CMakeCache.txt

build: configure
	cmake --build $(BUILD_DIR) --config Release -j$(JOBS)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure -C Release

run: build
	$(GUI_BIN)

clean:
	rm -rf $(BUILD_DIR)
