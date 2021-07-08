CCOBJ=clang -c -Wall -ansi -std=c99 -Werror -pedantic -DDEBUG
CCLINK=clang  -DDEBUG
CCLINKLIBS=-lpthread

OBJSLIB=tmp/electronctrl_serial.o
OBJSEXE=tmp/maincli.o

all: bin/electronctrl \
	bin/libelectronctrl.a


bin/libelectronctrl.a: $(OBJSLIB)

	ar -crs bin/libelectronctrl.a $(OBJSLIB)

bin/electronctrl: $(OBJSEXE) bin/libelectronctrl.a

	$(CCLINK) -L./bin/ -o bin/electronctrl $(OBJSEXE) $(CCLINKLIBS) -lelectronctrl

tmp/electronctrl_serial.o: src/electronctrl_serial.c src/electronctrl.h

	$(CCOBJ) -o tmp/electronctrl_serial.o src/electronctrl_serial.c

tmp/maincli.o: src/maincli.c src/electronctrl.h

	$(CCOBJ) -o tmp/maincli.o src/maincli.c
