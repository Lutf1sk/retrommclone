
OUT = retrommclone

OBJS = \
	src/main.o \
	src/render.o \
	src/websock.o \
	src/stb_image.o \
	src/npc.o \
	src/chest.o \
	src/bank.o \
	src/map.o \
	src/cosmetic.o \
	src/send.o \
	src/chat.o \
	src/resource.o

DEPS = $(patsubst %.o,%.deps,$(OBJS))

CC = cc
CC_FLAGS += -O2 -fmax-errors=3 -Wall -Werror -Wno-strict-aliasing -Wno-error=unused-variable -Wno-unused-function -Ilt/include/ -Wno-pedantic -std=c11

LNK = cc
LNK_FLAGS += -o $(OUT) -g

ifdef DEBUG
	CC_FLAGS += -g
endif

ifdef UBSAN
	LNK_LIBS += -lubsan
	CC_FLAGS += -fsanitize=undefined
endif

ifdef WINDOWS
	CC = x86_64-w64-mingw32-gcc
	LNK = x86_64-w64-mingw32-gcc
	LNK_LIBS += -lgdi32 -lws2_32 -lopengl32
	LNK_FLAGS += -static
else
	LNK_FLAGS += -rdynamic
	LNK_LIBS += -lpthread -ldl -lm
endif

LT_PATH = lt/bin/lt.a

all: $(LT_PATH) $(OUT)

$(LT_PATH):
	make -C lt/

run: all
	./$(OUT) test.nyx

prof: all
	valgrind --dump-instr=yes --tool=callgrind ./$(OUT) test.nyx
	kcachegrind ./callgrind.out*
	rm ./callgrind.out.*

$(OUT):	$(OBJS)
	$(LNK) $(LNK_FLAGS) $(OBJS) $(LT_PATH) $(LNK_LIBS)

%.o: %.c makefile
	$(CC) $(CC_FLAGS) -MM -MT $@ -MF $(patsubst %.o,%.deps,$@) $<
	$(CC) $(CC_FLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm $(OBJS) $(DEPS)

.PHONY: all clean run sync
