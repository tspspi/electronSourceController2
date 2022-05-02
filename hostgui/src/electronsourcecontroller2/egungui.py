import tkinter as tk
import time

from eguncom import ElectronGunControl

class EGunGUIMainWindow_VersionInfo(tk.Frame):
    def __init__(self, parent, ctrl, mainwnd):
        tk.Frame.__init__(self, parent)
        res = ctrl.id(sync = True)
        lblVersionInfo = tk.Label(self, text = "Controller version: {}".format(res))
        lblVersionInfo.pack()
        self.pack()

class EGunGUIMainWindow_Voltages(tk.Frame):
    def __init__(self, parent, ctrl, mainwnd):
        tk.Frame.__init__(self, parent)

        lblPSU1 = tk.Label(self, text = "PSU1 (Cathode):")
        lblPSU1.grid(row = 0, column = 0)
        lblPSU2 = tk.Label(self, text = "PSU2 (Whenelt):")
        lblPSU2.grid(row = 1, column = 0)
        lblPSU3 = tk.Label(self, text = "PSU3 (Focus / Cutoff):")
        lblPSU3.grid(row = 2, column = 0)
        lblPSU4 = tk.Label(self, text = "PSU4 (Auxiliary):")
        lblPSU4.grid(row = 3, column = 0)

        self.txtVoltCathode = tk.Entry(self)
        self.txtVoltCathode.grid(row = 0, column = 1)
        self.txtVoltWhenelt = tk.Entry(self)
        self.txtVoltWhenelt.grid(row = 1, column = 1)
        self.txtVoltFocus = tk.Entry(self)
        self.txtVoltFocus.grid(row = 2, column = 1)
        self.txtVoltAux = tk.Entry(self)
        self.txtVoltAux.grid(row = 3, column = 1)

        self.lblPSU1 = tk.Label(self, text = "0000.0 uA")
        self.lblPSU1.grid(row = 0, column = 2)
        self.lblPSU2 = tk.Label(self, text = "0000.0 uA")
        self.lblPSU2.grid(row = 1, column = 2)
        self.lblPSU3 = tk.Label(self, text = "0000.0 uA")
        self.lblPSU3.grid(row = 2, column = 2)
        self.lblPSU4 = tk.Label(self, text = "0000.0 uA")
        self.lblPSU4.grid(row = 3, column = 2)

        ctrl.cbVoltage.append(self.updateVoltage)

        self.pack()
    def updateVoltage(self, ctrl, channel, voltage):
        if channel == 1:
            self.lblPSU1.text = "{} V".format(voltage)
        if channel == 2:
            self.lblPSU2.text = "{} V".format(voltage)
        if channel == 3:
            self.lblPSU3.text = "{} V".format(voltage)
        if channel == 4:
            self.lblPSU4.text = "{} V".format(voltage)

class EGunGUIMainWindow_CommandButtons(tk.Frame):
    def __init__(self, parent, ctrl, mainwnd):
        tk.Frame.__init__(self, parent)

        self.ctrl = ctrl
        self.mainwnd = mainwnd

        self.btnInsulation = tk.Button(self, text = "Insulation test", command = self.cmdInsulationTest)
        self.btnInsulation.grid(row = 0, column = 0)
        self.btnBeamOn = tk.Button(self, text = "Beam on", fg = "red", command = self.cmdBeamOn)
        self.btnBeamOn.grid(row = 0, column = 1)
        self.btnOff = tk.Button(self, text = "Off", fg = "green", command = self.cmdOff)
        self.btnOff.grid(row = 0, column = 2)
        self.btnExit = tk.Button(self, text = "Exit", command = self.cmdExit)
        self.btnExit.grid(row = 0, column = 3)

        self.pack()

    def cmdOff(self):
        ctrl.off()
    def cmdInsulationTest(self):
        ctrl.runInsulationTest()
    def cmdExit(self):
        self.quit()
    def cmdBeamOn(self):
        ctrl.beamOn()

class EGunGUIMainWindow(tk.Frame):
    def __init__(self, parent, ctrl):
        ctrl.cbVoltage = []

        tk.Frame.__init__(self, parent)

        EGunGUIMainWindow_VersionInfo(self, ctrl, self)
        EGunGUIMainWindow_Voltages(self, ctrl, self)
        EGunGUIMainWindow_CommandButtons(self, ctrl, self)

if __name__ == "__main__":
    root = tk.Tk()
    time.sleep(5)
    with ElectronGunControl() as ctrl:
        EGunGUIMainWindow(root, ctrl).pack(side="top", fill="both", expand=True)
        root.mainloop()
