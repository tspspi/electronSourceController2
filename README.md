# Electron gun power supply and monitoring controller

_Note_: This project is designed for a particular experiment so it will not
be much of use for anyone who's not working on exactly that experiment.

This is firmware for a simple electron gun controller that is controlling

* Up to four FUG HCP 14-6500 high voltage power supplies (acceleration
  voltage, Wehnelt cylinder, focus voltage and a single deflection voltage
  power supply)
* The voltage and current measured by FUG HCP 14-6500 can also be used for
  general monitoring and beam loss measurement

## External communication ports

* UART0 provides main communication interface (and is also used by avrdude
  during flashing - there is an Arduino bootloader flashed on the board so
  flash method ```ẁiring``` can be used via USB

* Other UARTs may be used for RS485 or other components later on

## Pin assignment

| Pin | Assignment                          | Mode                      | ATEMGA2560 Port / Pin |
| --- | ----------------------------------- | ------------------------- | --------------------- |
| 22  | PSU1: Output enable                 | Digital out (optocoupled) | PA0                   |
| 23  | PSU1: Polarity set                  | Digital out (optocoupled) | PA1                   |
| 24  | PSU1: Current control mode          | Digital in                | PA2                   |
| 25  | PSU1: Voltage control mode          | Digital in                | PA3                   |
| 42  | PSU1: Voltage set                   | Analog out                | PL7                   |
| 43  | PSU1: Current limit set             | Analog out                | PL6                   |
| A0  | PSU1: Voltage sense                 | Analog in                 | PF0                   |
| A1  | PSU1: Current sense                 | Analog in                 | PF1                   |
| 26  | PSU2: Output enable                 | Digital out (optocoupled) | PA4                   |
| 27  | PSU2: Polarity set                  | Digital out (optocoupled) | PA5                   |
| 28  | PSU2: Current control mode          | Digital in                | PA6                   |
| 29  | PSU2: Voltage control mode          | Digital in                | PA7                   |
| 44  | PSU2: Voltage set                   | Analog out                | PL5                   |
| 45  | PSU2: Current limit set             | Analog out                | PL4                   |
| A2  | PSU2: Voltage sense                 | Analog in                 | PF2                   |
| A3  | PSU2: Current sense                 | Analog in                 | PF3                   |
| 30  | PSU3: Output enable                 | Digital out (optocoupled) | PC7                   |
| 31  | PSU3: Polarity set                  | Digital out (optocoupled) | PC6                   |
| 32  | PSU3: Current control mode          | Digital in                | PC5                   |
| 33  | PSU3: Voltage control mode          | Digital in                | PC4                   |
| 46  | PSU3: Voltage set                   | Analog out                | PL3                   |
| 47  | PSU3: Current limit set             | Analog out                | PL2                   |
| A4  | PSU3: Voltage sense                 | Analog in                 | PF4                   |
| A5  | PSU3: Current sense                 | Analog in                 | PF5                   |
| 34  | PSU4: Output enable                 | Digital out (optocoupled) | PC3                   |
| 35  | PSU4: Polarity set                  | Digital out (optocoupled) | PC2                   |
| 36  | PSU4: Current control mode          | Digital in                | PC1                   |
| 37  | PSU4: Voltage control mode          | Digital in                | PC0                   |
| 48  | PSU4: Voltage set                   | Analog out                | PL1                   |
| 49  | PSU4: Current limit set             | Analog out                | PL0                   |
| A6  | PSU4: Voltage sense                 | Analog in                 | PF6                   |
| A7  | PSU4: Current sense                 | Analog in                 | PF7                   |
| 38  | Filament power supply (230V side)   | Digital out               | PD7                   |
| A8  | Current sensor for filament         | Analog in                 | PK0                   |
| 50  | AD7705 MISO                         | Digital in                | PB3                   |
| 51  | AD7705 MOSI                         | Digital out               | PB2                   |
| 52  | AD7705 SCK                          | Digital out               | PB1                   |
| 53  | AD7705 CS                           | Digital out               | PB0                   |
|PWM10| AD7705 Reset                        | Digital out               | PB4                   |
|PWM11| AD7705 data ready                   | Digital in                | PB5                   |

## Protocol

In contrast to other implementations this board supports an ASCII based
communication protocol. For synchronization all messages start with a
fixed synchronization pattern consisting of three dollar signs. Each command
is terminated via a CR LF or only LF (CR is simply ignored by the serial
parser). Numbers are transmitted as ASCII decimal numbers.

| Command                                 | Sequence               | Response                                                                                                                     | Status                           |
| --------------------------------------- | ---------------------- | ---------------------------------------------------------------------------------------------------------------------------- | -------------------------------- |
| ID                                      | $$$ID<LF>              | Sends a simple ID response that identifies the firmware (ex. ```$$$electronctrl_20210707_001<LF>```)                         | working, tested                  |
| Filament on                             | $$$FILON<LF>           | Switches filament power supply on                                                                                            | working, tested                  |
| Filament off                            | $$$FILOFF<LF>          | Switches filament power supply off                                                                                           | working, tested                  |
| Estimate beam current                   | $$$BEAMA<LF>           | Calculates estimated beam current in 1/10 of microamperes                                                                    |                                  |
| Beam on                                 | $$$BEAMON<LF>          | Switches beam high voltage on (slowly, performing insulation test)                                                           | working, tested                  |
| Beam HV off                             | $$$BEAMHVOFF<LF>       | Switches beam high voltage off                                                                                               | working, tested                  |
| Get PSU current                         | $$$PSUGETA[n]<LF>      | Gets current for power supply 1,2,3,4                                                                                        | working, tested                  |
| Get PSU voltage                         | $$$PSUGETV[n]<LF>      | Gets voltage for power supply 1,2,3,4                                                                                        | working, tested                  |
| Get PSU modes                           | $$$PSUMODE<LF>         | Gets the mode for each PSU as a sequence of 4 ASCII chars (A or V for current or voltage controled mode, - for disabled)     | working, tested                  |
| Set PSU current limit                   | $$$PSUSETA[n][mmm]<LF> | Sets the power supply current limit for one of the 4 PSUs. The limit is supplied in 1/10 of an microampere                   | working, tested                  |
| Set PSU target voltage                  | $$$PSUSETV[n][mmm]<LF> | Sets the power supply voltage for one of the 4 PSUs. The voltage is set in V                                                 | working, tested                  |
| Set PSU polarity                        | $$$PSUPOL[n][p/n]<LF>  | Sets the polarity to be positive or negative                                                                                 | working, tested                  |
| Set PSU output enable                   | $$$PSUON[n]<LF>        | Enabled the output of the given PSU                                                                                          | working, tested                  |
| Set PSU output disable                  | $$$PSUOFF[n]<LF>       | Disabled the output of the given PSU                                                                                         | working, tested                  |
| Disable all voltages (PSU and filament) | $$$OFF<LF>             | Disabled all voltages including the filament supply                                                                          | working, tested                  |
| Get filament voltage                    | $$$FILV<LF>            | Measures filament voltage (if supported)                                                                                     |                                  |
| Get filament current                    | $$$FILA<LF>            | Measures filament current (if supported)                                                                                     |                                  |
| Set filament current                    | $$$SETFILA[n]<LF>      | Sets filament current via current controller board                                                                           |                                  |
| Perform HV insulation test              | $$$INSUL<LF>           | Performs insulation test. Reports voltages while testing.                                                                    | working, tested                  |
| Perform cathode bakeout                 | $$$FILBAKE<LF>         | Performs a baking sequence on filament to remove any residue of trapped gas on the surface                                   |                                  |
| Perform software reset                  | $$$RESET<LF>           | Performs a software reset on the control system (reinitializes everything, no power cycle)                                   |                                  |
