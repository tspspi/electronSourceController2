from tkinter import Tk, RIGHT, BOTH, RAISED
from tkinter.ttk import Frame, Label, Button, Style, Entry

class EGunGUI_WindowCommand(Frame):
    def __init__(self, root):
        super().__init__()
        self.master.title("E-gun control")

class EGunGUI_WindowConnect(Frame):
    def __init__(self, root):
        super().__init__()
        self.root = root

        self.master.title("E-gun")

        labelPort = Label(text = "Serial port")
        labelPort.pack()
        self.txtPortFile = Entry(text = "/dev/ttyU0")
        self.txtPortFile.pack(fill = BOTH)
        btnConnect = Button(text = "Connect", command = self.btnConnect)
        btnConnect.pack(fill = BOTH)
        btnClose = Button(text = "Close", command = self.btnClose)
        btnClose.pack(fill = BOTH)

    def btnClose(self):
        self.root.destroy()

    def btnConnect(self):
        portFile = self.txtPortFile.get()
        self.root.destroy()




if __name__ == '__main__':
    root = Tk()
    EGunGUI_WindowConnect(root)
    root.mainloop()
