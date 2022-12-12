CC				:= g++
DBG				:= -g -Wall -Werror
OPT				:= -O2
CXXFLAGS	:= $(DBG) $(OPT) -fPIC
LDFLAGS		:= -shared -lGL -ldl

SRC	:= draw.cpp
HDR := draw.h
LIB	:= $(addprefix lib, $(addsuffix .so, $(basename $(SRC))))

.PHONY: all clean

all: $(LIB)

$(LIB): $(SRC) $(HDR)
	$(CC) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(LIB)
