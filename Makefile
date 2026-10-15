TARGET = devreset

all: $(TARGET)

# Universal binary (x86_64 + arm64) in one step; clang merges the slices.
# arm64 has no macOS before 11, so its slice is raised to 11 automatically.
$(TARGET): $(TARGET).c
	gcc $(TARGET).c -framework IOKit -framework CoreFoundation -o $(TARGET) \
		-arch x86_64 -arch arm64 -mmacosx-version-min=10.12

clean:
	$(RM) $(TARGET)
