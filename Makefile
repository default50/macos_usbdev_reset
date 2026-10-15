TARGET = devreset
TEST_BIN = tests/test_devreset
TEST_SRCS = devreset.c tests/fake_usb_ops.c tests/test_devreset.c

.PHONY: all clean test

all: $(TARGET)

# Universal binary (x86_64 + arm64) in one step; clang merges the slices.
# arm64 has no macOS before 11, so its slice is raised to 11 automatically.
$(TARGET): devreset.c usb_ops_iokit.c usb_ops.h
	gcc -Wall -Wextra devreset.c usb_ops_iokit.c -framework IOKit -framework CoreFoundation -o $(TARGET) \
		-arch x86_64 -arch arm64 -mmacosx-version-min=10.12

# Native architecture only, no frameworks: the fake replaces IOKit.
$(TEST_BIN): $(TEST_SRCS) usb_ops.h tests/fake_usb_ops.h tests/greatest.h
	gcc -Wall -Wextra -DUNIT_TEST -I. -Itests $(TEST_SRCS) -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	$(RM) $(TARGET) $(TEST_BIN)
