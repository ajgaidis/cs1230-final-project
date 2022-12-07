CC			:= gcc
DBG			:= -g -Wall -Werror
OPT			:= -O2
CFLAGS	:= $(DBG) $(OPT) -fPIC $(if $(v),-DVERBOSE=1,)
LDFLAGS	:= -pie

SRC	:= main.c
BIN	:= $(basename $(SRC))

.PHONY: help all clean

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

help:
	@echo "Run 'make v=1' to build a verbose version of the binary."

clean:
	rm -f $(BIN)
