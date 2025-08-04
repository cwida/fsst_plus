# Define the build directories
BUILD_DIR = build
DEBUG_DIR = $(BUILD_DIR)/debug
RELEASE_DIR = $(BUILD_DIR)/release
TARGET = fsst-plus

# Default rule: Build the project in Debug mode
all: debug

# Build the project in Debug mode
debug:
	@mkdir -p $(DEBUG_DIR)
	cd $(DEBUG_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug ../.. && $(MAKE)

# Build the project in Release mode
release:
	@mkdir -p $(RELEASE_DIR)
	cd $(RELEASE_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ../.. && $(MAKE)

# Clean rule: Remove all build artifacts
clean:
	@rm -rf $(BUILD_DIR)

# Run the compiled binary (Debug mode)
run: debug
	@./$(DEBUG_DIR)/$(TARGET)

# Run the compiled binary (Release mode)
run-release: release
	@./$(RELEASE_DIR)/$(TARGET)