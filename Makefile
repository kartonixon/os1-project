CC=gcc
CFLAGS= -std=gnu99 -Wall
LDLIBS= -lpthread -lm

all: rmg

rmg:
	${CC} ${CFLAGS} -o rmg rmg.c ${LDLIBS}

.PHONY: clean

clean:
	rm rmg
