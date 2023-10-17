#ifndef __is_included__b4fb2fa4_6f1b_11ed_b682_b499badf00a1
#define __is_included__b4fb2fa4_6f1b_11ed_b682_b499badf00a1 1

#ifdef __cplusplus
	extern "C" {
#endif

#define EEPROM_OFFSET_CFG 0

struct cfgOptions {
	uint8_t chksum;
	uint16_t magic;

	struct {
		unsigned long int cathode;
		unsigned long int wehneltCylinder;
		unsigned long int wehneltCylinderBlank;
		unsigned long int focus;
		unsigned long int aux;
	} beamOnRampTargets;

	struct {
		unsigned long int cathode;
		unsigned long int wehneltCylinder;
		unsigned long int focus;
		unsigned long int aux;
	} beamOnCurrentLimits;
	struct {
		unsigned long int cathode;
		unsigned long int wehneltCylinder;
		unsigned long int focus;
		unsigned long int aux;
	} insulationCurrentLimits;

	struct {
		unsigned long int stepsizeV;
		unsigned long int stepsizeFila;
		unsigned long int stepDuration;
		unsigned long int initDuration;
		unsigned long int stepDurationFilament;
	} ramps;

	struct {
		struct {
			/* Just a simple linear model ...*/
			double k;
			double d;

			/* Calibration values */
			uint16_t adc0;
			uint16_t adc1;
			uint16_t vhigh;
		} channel[8];
	} psuADCCalibration;
};

void cfgeepromLoad();
void cfgeepromStore();

#ifndef __in_module__1eed2317_6f1c_11ed_b682_b499badf00a1
	extern struct cfgOptions cfgOptions;
#endif



#ifdef __cplusplus
	} /* extern "C" { */
#endif



#endif /* __is_included__b4fb2fa4_6f1b_11ed_b682_b499badf00a1 */
