# Custom electron gun slow control Python communication library and GUI utlity

__Work in progress__

## Communication library

The communication library from ```eguncom.py``` implements most of the
communication protocol with the electron beam slow-control system.

### ```class ElectronGunControl```

The ```ElectronGunControl``` class implements asynchronous and synchronous
communication with the serially / USB attached control system. The port file
is passed to the constructor

```
ctrl = ElectronGunNotConnected(portFile = '/dev/...')
```

When no port file is passed it defaults to ```/dev/ttyU0```.

The exposed public methods are:

| Method                            | Description                                                                                                        | Synchronous      | Asynchronous callback                                |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------ | ---------------- | ---------------------------------------------------- |
| id()                              | Queries the current version                                                                                        | with sync = True | cbIdentify(controller, versionDate, versionRevision) |
| getPSUVoltage(channel)            | Queries the current voltage on the given channel                                                                   | with sync = True | cbVoltage(controller, channel, voltageVolts)         |
| getPSUCurrent(channel)            | Queries the current current on the given channel                                                                   | with sync = True | cbCurrent(controller, channel, currentMicroamps)     |
| getPSUModes()                     | Checks if PSUs are off, in voltage limited or current limited mode                                                 | with sync = True | cbPSUMode(controller, states)                        |
| getFilamentCurrent()              | Queries the filament current                                                                                       | with sync = True | cbFilamentCurrent(controller, current)               |
| off()                             | Disabled all high voltage and filament currents                                                                    |                  |                                                      |
| setPSUPolarity(channel, polarity) | Sets polarity to POLARITY_POS or POLARITY_NEG                                                                      |                  |                                                      |
| setPSUEnable(channel)             | Enables the given PSU channel                                                                                      |                  |                                                      |
| setPSUDisable(channel)            | Disables the given PSU channel                                                                                     |                  |                                                      |
| setPSUVoltage(channel, voltage)   | Sets the given channel to the given voltage                                                                        |                  |                                                      |
| setFilamentCurrent(currentMa)     | Sets the filament current or target current to the specified milliamps                                             |                  |                                                      |
| setFilamentOn()                   | Enabled the filament supply                                                                                        |                  |                                                      |
| setFilamentOff()                  | Disabled the filament supply                                                                                       |                  |                                                      |
| runInsulationTest()               | Runs an insulation test with low current limits                                                                    | with sync = True | cbInsulation(isOk, listOfShorts)                     |
| beamOn()                          | Runs the slow beam on sequence (slowly heating filament to previously set setFilamentCurrent, ramping up voltages) | with sync = True | cbBeamon()                                           |

### Usage example

#### Synchronous sample (USB / serial)

Example to perform a __synchronous__:

* Insulation check
* Startup for 100 mA beam current
* Looping through focus voltages
* Shutting down

```
with ElectronGunControl() as ctrl:
    # Currently a hack to wait for the control system to reset when using the
    # primary port

    time.sleep(5)

    # We want to see the voltages reported by the system so we
    # define a lambda that just prints the voltages while ramping up
    # during insulation test and beam on sequence

    ctrl.cbVoltage = lambda c,chan,v : print("Voltage for channel {} is {} V".format(chan, v))

    # Query it's identity
    ret = ctrl.id(sync = True)
    print("ID call returned {}".format(ret))

    # Run insulation test
    print("Running insulation test")
    ret = ctrl.runInsulationTest(sync = True)
    print("Insulation test returned: {}".format(ret))

    # Run beam on sequence ...
    ret = ctrl.beamOn(sync = True)
    print("Beam on successfully returned: {}".format(ret))

    print("Sweeping focus voltage")
    for focusVoltage in range(1750,1950):
        print("New focus voltage {}".format(focusVoltage))
        ctrl.setPSUVoltage(3, focusVoltage)
        time.sleep(1)

    ctrl.off()
    print("Off, done ...")
```
