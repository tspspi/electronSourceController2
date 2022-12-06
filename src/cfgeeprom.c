#define __in_module__1eed2317_6f1c_11ed_b682_b499badf00a1 1

#include <avr/eeprom.h>
#include <stdint.h>

#include "./controller.h"
#include "./cfgeeprom.h"

#ifdef __cplusplus
	extern "C" {
#endif

struct cfgOptions cfgOptions;


struct cfgOptions cfgOptions_Default = {
	0x00,	/* Checksum */
	0xAA55, /* Magic value */
	{ /* Target voltages for beam on and insulation test */
		2000,	/* Cathode */
		2020,	/* Whenelt */
		1980,	/* Focus */
		0 /* Aux */
	},
	{ /* Current limits: Beam on */ 900, 900, 900, 10 },
	{ /* Current limits: Insulat ion test */ 10, 10, 10, 10 },
	{
		5, /* Voltage stepsize */
		5, /* Filament stepsize */
		900000, /* Step duration */
		10000000, /* Init duration */
		250000 /* Filament step duration */
	},
	{
		/* PSU readout defaults */
		{
			{ 3.221407, 0.0 }, { 9.765625 },
			{ 3.221407, 0.0 }, { 9.765625 },
			{ 3.221407, 0.0 }, { 9.765625 },
			{ 3.221407, 0.0 }, { 9.765625 }
		}
	}
};

void cfgeepromDefaults() {
	unsigned long int i;
	for(i = 0; i < sizeof(struct cfgOptions); i=i+1) {
		((uint8_t*)(&cfgOptions))[i] = ((uint8_t*)(&cfgOptions_Default))[i];
	}
	cfgeepromStore();
}

void cfgeepromLoad() {
	unsigned long int i;

	eeprom_read_block(&cfgOptions, EEPROM_OFFSET_CFG, sizeof(struct cfgOptions));
	uint8_t chkSum = 0x00;

	for(i = 0; i < sizeof(struct cfgOptions); i=i+1) {
		chkSum = chkSum ^ ((uint8_t*)(&cfgOptions))[i];
	}
	if((chkSum != 0x00) || (cfgOptions.magic != 0xAA55)) {
		cfgeepromDefaults();
	}
}
void cfgeepromStore() {
	uint8_t chkSum = 0x00;
	unsigned long int i;

	cfgOptions.chksum = 0x00;

	for(i = 0; i < sizeof(struct cfgOptions); i=i+1) {
		chkSum = chkSum ^ ((uint8_t*)(&cfgOptions))[i];
	}
	cfgOptions.chksum = chkSum;

	eeprom_write_block(&cfgOptions, EEPROM_OFFSET_CFG, sizeof(cfgOptions));
}

#ifdef __cplusplus
	} /* extern "C" { */
#endif
