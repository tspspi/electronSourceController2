CPUFREQ=16000000L
FLASHDEV=/dev/ttyU0
FLASHBAUD=115200
FLASHMETHOD=wiring

SRCFILES=src/controller.c \
	src/serial.c \
	src/sysclock.c \
	src/adc.c
HEADFILES=src/controller.h \
	src/serial.h \
	src/sysclock.h \
	src/adc.h

all: bin/controller.hex

bin/controller.bin: $(SRCFILES) $(HEADFILES)

	avr-gcc -Wall -Os -mmcu=atmega2560 -DF_CPU=$(CPUFREQ) -o bin/controller.bin $(SRCFILES)

bin/controller.hex: bin/controller.bin

	avr-size -t bin/controller.bin
	avr-objcopy -j .text -j .data -O ihex bin/controller.bin bin/controller.hex

flash: bin/controller.hex

	sudo chmod 666 $(FLASHDEV)
	avrdude -v -p atmega2560 -c $(FLASHMETHOD) -P $(FLASHDEV) -b $(FLASHBAUD) -D -U flash:w:bin/controller.hex:i

framac: $(SRCFILES)

	-rm framacreport.csv
	frama-c -wp-verbose 0 -wp -rte -wp-rte -wp-dynamic -wp-timeout 300 -cpp-extra-args="-I/usr/home/tsp/framaclib/ -DF_CPU=16000000L -D__AVR_ATmega2560__ -DFRAMAC_SKIP" $(SRCFILES) -then -no-unicode -report -report-csv framacreport.csv

clean:

	-rm *.bin
	-rm bin/*.bin

cleanall: clean

	-rm *.hex
	-rm bin/*.hex

.PHONY: all clean cleanall
