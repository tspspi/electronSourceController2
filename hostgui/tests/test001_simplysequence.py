import time

from electronsourcecontroller2.eguncom import ElectronGunControl

if __name__ == "__main__":
    with ElectronGunControl() as ctrl:
        #ctrl.cbIdentify = lambda c,v,r : print("Version {}, revision {}\n".format(v,r))
        #ctrl.cbInsulation = lambda c,isOk,failedList : print("Insulation is {}, failed list: {}".format(isOk, failedList))
        #ctrl.cbVoltage = lambda c,chan,v : print("Voltage for channel {} is {} V".format(chan, v))
        #ctrl.cbCurrent = lambda c,chan,a : print("Current for channel {} is {} uA".format(chan, a))
        #ctrl.cbPSUMode = lambda c,modes : print("Power supply modes: {}".format(modes))
        #ctrl.cbFilamentCurrent = lambda c,current : print("Filament current: {}".format(current))
        #ctrl.cbFilamentCurrentSet = lambda c,current : print("Filament current set to {}".format(current))
        #ctrl.cbBeamon = lambda c : print("Beam on ...")

        time.sleep(10)
        ret = ctrl.id(sync = True)
        print("ID call returned {}".format(ret))
        ret = ctrl.getPSUModes(sync = True)
        print("Get PSU modes returned {}".format(ret))

        print("Running insulation test")
        ret = ctrl.runInsulationTest(sync = True)
        print("Returned: {}".format(ret))

        print("Setting filament current to 100 mA")
        ret = ctrl.setFilamentCurrent(100)

        print("Running beam on sequence")
        ret = ctrl.beamOn(sync = True)
        print("Returned: {}".format(ret))

        print("Sweeping focus voltage")
        for focusVoltage in range(1750,1950):
            print("New focus voltage {}".format(focusVoltage))
            ctrl.setPSUVoltage(3, focusVoltage)
            time.sleep(1)

        ctrl.off()
        print("Off ...")
