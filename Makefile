MODE     := native
ifeq ($(MODE), dev)
	OFLAGS := -g -O0 -DDEBUG
else
	OFLAGS := -O3 -march=native -fwrapv -ffast-math
endif
CPPFLAGS :=
CFLAGS   := -std=c11 -pedantic -Wall $(OFLAGS)
LDFLAGS  := -lc -lm

CC = cc

TARGET_EXE := bigfort

BUILD_DIR := build
SRC_DIR   := src
INC_DIR   := src lib/cubiomes

CPPFLAGS += $(addprefix -I,$(INC_DIR))

SRC  := src/main.c
OBJ  := $(addprefix $(BUILD_DIR)/, $(notdir $(SRC:.c=.o)))
LIBS := lib/cubiomes/libcubiomes.a

all: $(BUILD_DIR) $(BUILD_DIR)/$(TARGET_EXE)

$(BUILD_DIR)/$(TARGET_EXE): $(OBJ) $(LIBS)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

lib/cubiomes/libcubiomes.a:
	$(MAKE) -C lib/cubiomes native

clean:
	rm -r $(BUILD_DIR)
	$(MAKE) -C lib/cubiomes clean

.PHONY: all clean
