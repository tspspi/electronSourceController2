CCOBJ=clang -c -Wall -ansi -std=c99 -Werror -pedantic -DDEBUG
CCLINK=clang  -DDEBUG
CCLINKLIBS=-lpthread

OBJS=tmp/electronctrl_serial.o \
	tmp/maincli.o

all: bin/electronctrl


bin/electronctrl: $(OBJS)

	$(CCLINK) -o bin/electronctrl $(OBJS) $(CCLINKLIBS)

tmp/electronctrl_serial.o: src/electronctrl_serial.c src/electronctrl.h

	$(CCOBJ) -o tmp/electronctrl_serial.o src/electronctrl_serial.c

tmp/maincli.o: src/maincli.c src/electronctrl.h

	$(CCOBJ) -o tmp/maincli.o src/maincli.c
