BUILD_DIR ?= build

.PHONY: all clean run doxygen

all:
	cmake -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
	rm -rf doc/html doc/latex

run: all
	./$(BUILD_DIR)/fsm-editor

doxygen:
	doxygen Doxyfile