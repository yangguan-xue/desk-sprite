CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c99 -I../src/include -I.

SRC = main.c sim_audio.c sim_mqtt.c ../src/ui/ui_layout.c
TARGET = desk_sprite_sim

# 检测是否有 SDL2
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)

ifeq ($(SDL2_LIBS),)
ifneq ($(wildcard /opt/homebrew/include/SDL2),)
SDL2_CFLAGS := -I/opt/homebrew/include -D_THREAD_SAFE
SDL2_LIBS := -L/opt/homebrew/lib -lSDL2
endif
endif

ifneq ($(SDL2_LIBS),)
CFLAGS += $(SDL2_CFLAGS) -DHAS_SDL2
LDFLAGS += $(SDL2_LIBS)
endif

# 检测是否有 mosquitto
MOSQ_LIBS := $(shell pkg-config --libs libmosquitto 2>/dev/null)
ifneq ($(MOSQ_LIBS),)
CFLAGS += -DHAS_MOSQUITTO
LDFLAGS += $(MOSQ_LIBS)
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

test_build:
	$(CC) $(CFLAGS) -DHEADLESS_TEST $(SRC) -o $(TARGET) $(LDFLAGS)
	./$(TARGET) --headless --once

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all test_build run clean
