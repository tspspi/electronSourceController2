import serial
import threading
import time
import atexit
import json

egun_version = "0.0.37 (Sat, 2022-11-24)"

print(f"Electron source controller: {egun_version}")

from collections import deque

class ElectronGunRingbuffer:
    def __init__(self, bufferSize = 512):
        self.bufferSize = bufferSize
        self.buffer = [ ]
        for i in range(512):
            self.buffer.append(None)
        self.head = 0
        self.tail = 0
        self.lock = threading.Lock()

    def isAvailable(self, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        if self.head == self.tail:
            if blocking:
                self.lock.release()
            return False
        else:
            if blocking:
                self.lock.release()
            return True

    def available(self, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        res = 0
        if self.head >= self.tail:
            res = self.head - self.tail;
        else:
            res = self.bufferSize - self.tail + self.head
        if blocking:
            self.lock.release()
        return res

    def discard(self, len, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        avail = self.available(blocking = False)
        if avail < len:
            self.tail = (self.tail + avail) % self.bufferSize
        else:
            self.tail = (self.tail + len) % self.bufferSize
        if blocking:
            self.lock.release()
        return None

    def peek(self, distance, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        if distance >= self.available(blocking = False):
            if blocking:
                self.lock.release()
            return None
        else:
            res = self.buffer[(self.tail + distance) % self.bufferSize]
            if blocking:
                self.lock.release()
            return res

    def remainingCapacity(self, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        res = self.bufferSize - self.available(blocking = False) - 1
        if blocking:
            self.lock.release()
        return res

    def capacity(self, *ignore, blocking = True):
        return self.bufferSize

    def push(self, data, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        if isinstance(data, list):
            if self.remainingCapacity(blocking = False) <  len(data):
                # Raise error ... ToDo
                if blocking:
                    self.lock.release()
                return
            else:
                for c in data:
                    self.push(c, blocking = False)
        else:
            if self.remainingCapacity(blocking = False) < 1:
                # Raise error ... ToDo
                if blocking:
                    self.lock.release()
                return
            self.buffer[self.head] = data
            self.head = (self.head + 1) % self.bufferSize
        self.lock.release()

    def pop(self, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        ret = None
        if self.head != self.tail:
            ret = self.buffer[self.tail]
            self.tail = (self.tail + 1) % self.bufferSize
        if blocking:
            self.lock.release()
        return ret

    def read(self, len, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        if self.available(blocking = False) < len:
            # ToDo Raise exception
            if blocking:
                self.lock.release()
            return None
        ret = [ ]
        for i in range(len):
            ret.append(self.buffer[(self.tail + i) % self.bufferSize])
        self.tail = (self.tail + len) % self.bufferSize
        if blocking:
            self.lock.release()
        return ret


class ElectronGunException(Exception):
    pass
class ElectronGunInvalidParameterException(Exception):
    pass
class ElectronGunCommunicationException(ElectronGunException):
    pass
class ElectronGunNotConnected(ElectronGunCommunicationException):
    pass

class ElectronGunControl:
    POLARITY_POS = b'p'
    POLARITY_NEG = b'n'

    def __init__(self, portFile = None, commandRetries = 3, shutdownOnTerminate = True):
        self._shutdownOnTerminate = shutdownOnTerminate
        if not portFile:
            portFile = '/dev/ttyU0'
            try:
                from quakesrctrl import config
                if config.VISA_ID_EBEAM:
                    portFile = config.VISA_ID_EBEAM
            except Exception:
                portFile = '/dev/ttyU0'
                pass

        self.bufferInput = ElectronGunRingbuffer()

        self._commandRetries = commandRetries

        self.port = False
        self.portFile = portFile
        self.port = serial.Serial(portFile, baudrate=19200, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, timeout=30)
        self.thrProcessing = threading.Thread(target=self.communicationThreadMain)
        self.thrProcessing.start()
        self.stabilizationDelay = 5

        self.defaultVoltages = [ 2018, 2020, 1808, 0 ]
        self.currentVoltages = [ 0, 0, 0, 0 ]

        atexit.register(self.close)

        # Condition variable used to implement synchronous calls
        # for execution from jupyter notebook, synchronous simple scripts, etc.
        #
        # messageFilter contains the name of the message the application wants to wait for
        # messageResponse will contain the payload decoded by the communication thread after the filter has matched
        self.messageFilter = None
        self.messageResponse = None
        self.messageConditionVariable = threading.Condition()

        self.cbIdentify = None
        self.cbVoltage = None
        self.cbCurrent = None
        self.cbPSUMode = None
        self.cbFilamentCurrent = None
        self.cbInsulation = None
        self.cbBeamon = None
        self.cbFilamentCurrentSet = None
        self.cbOff = None

        # Currently introduce a delay to wait for the AVR board to reboot just in
        # case the main USB port has been used. Not a really clean solution but
        # it works and usually one does not reinitialize too often
        #time.sleep(10)

    def shutdown_on_terminate(self, shutdown):
        self._shutdownOnTerminate = shutdown

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        atexit.unregister(self.close)
        if self.port:
            self.port.close()
            self.port = False
        if self.thrProcessing:
            self.thrProcessing.join()
            self.thrProcessing = False

    def close(self):
        atexit.unregister(self.close)
        if self.port:
            try:
                if self._shutdownOnTerminate:
                    self.off(sync = True)
            except Exception:
                # Simply ignore all exceptions
                pass
            self.port.close()
            self.port = False
        if self.thrProcessing:
            self.thrProcessing.join()
            self.thrProcessing = False

    def internal__waitForMessageFilter(self, filter):
        self.messageConditionVariable.acquire()

        self.messageFilter = filter

        # Now wait till we get a response from our processing thread ...
        remainingRetries = int(self._commandRetries)
        if remainingRetries is None:
            remainingRetries = 0
        elif remainingRetries < 0:
            remainingRetries = 0

        while self.messageResponse == None:
            if self.messageFilter == "beamon":
                mytimeout = 1200
            else:
                mytimeout = 10
            if not self.messageConditionVariable.wait(timeout = mytimeout):
                print(f"Timeout while waiting for {self.messageFilter} (Retries remaining: {remainingRetries})")
                if (remainingRetries > 0) and (self._lastcommand is not None):
                    # Retry command ...
                    self.port.write(self._lastcommand)
                    remainingRetries = remainingRetries - 1
                else:
                    self.jabber(sync = False)
                    self.messageFilter = None
                    self.messageResponse = None
                    self._lastcommand = None
                    self.messageConditionVariable.release()
                    return None
        # Reset message filter, copy and release response ...
        self._lastcommand = None
        self.messageFilter = None
        retval = self.messageResponse
        self.messageResponse = None

        # Release ...
        self.messageConditionVariable.release()
        return retval

    def internal__signalCondition(self, messageType, payload):
        self.messageConditionVariable.acquire()
        if messageType != self.messageFilter:
            self.messageConditionVariable.release()
            return
        self.messageResponse = payload
        self.messageConditionVariable.notify_all()
        self.messageConditionVariable.release()

    def communicationThreadMain_HandleMessage(self, msgb):
        # Decode message and if a callback is registered call it. Note that event
        # handlers are usually lists of functions or functions (see documentation)
        #
        # Note for synchronous commands there is a mutex that unblocks
        # when the specified message has arrived

        # Assemble string from message byte array
        # (and verify the characters are in fact legal)
        msg=''.join([x.decode('utf-8') for x in msgb])
        msg = msg[3:]
        try:
            if msg[0:len("electronctrl")] == "electronctrl":
                # Response to ID packet
                parts = msg.split("_")
                versionDate = parts[1]
                versionRev = parts[2]

                if self.cbIdentify:
                    if type(self.cbIdentify) is list:
                        for f in self.cbIdentify:
                            if callable(f):
                                f(self, versionDate, versionRev)
                    elif callable(self.cbIdentify):
                        self.cbIdentify(self, versionDate, versionRev)

                self.internal__signalCondition("id", { 'version' : versionDate, 'revision' : versionRev })
            elif msg[0:len("id:")] == "id:":
                # ToDo (Current controller)
                pass
            elif msg[0:len("ver:")] == "ver:":
                # ToDo (Current controller)
                pass
            elif msg[0:len("beamon")] == "beamon":
                if self.cbBeamon:
                    if type(self.cbBeamon) is list:
                        for f in self.cbBeamon:
                            if callable(f):
                                f(self)
                    elif callable(self.cbBeamon):
                        self.cbBeamon(self)

                self.internal__signalCondition("beamon", True)
            elif msg[0:len("insulok")] == "insulok":
                if self.cbInsulation:
                    if type(self.cbInsulation) is list:
                        for f in self.cbInsulation:
                            if callable(f):
                                f(self, True, None)
                    elif callable(self.cbInsulation):
                        self.cbInsulation(self, True, None)

                self.internal__signalCondition("insulok", True )
            elif msg[0:len("insulfailed")] == "insulfailed":
                failedPSUs = []
                for i in range(4):
                    if msg[len("insulfailed:") + i] == 'F':
                        failedPSUs.append(i+1)
                if self.cbInsulation:
                    if type(self.cbInsulation) is list:
                        for f in self.cbInsulation:
                            if callable(f):
                                f(self, False, failedPSUs)
                    elif callable(self.cbInsulation):
                        self.cbInsulation(self, False, failedPSUs)

                self.internal__signalCondition("insulok", failedPSUs)
            elif (msg[0] == 'v') and (msg[2] == ':'):
                try:
                    channel = int(msg[1])
                    voltage = int(msg[3:])
                    if self.cbVoltage:
                        if type(self.cbVoltage) is list:
                            for f in self.cbVoltage:
                                if callable(f):
                                    f(self, channel, voltage)
                        elif callable(self.cbVoltage):
                            self.cbVoltage(self, channel, voltage)
                    self.internal__signalCondition("v{}".format(channel), voltage )
                except ValueError:
                    pass
            elif (msg[0] == 'a') and (msg[2] == ':'):
                try:
                    if msg[1] == 'f':
                        current = int(msg[3:])
                        if self.cbFilamentCurrent:
                            if type(self.cbFilamentCurrent) is list:
                                for f in self.cbFilamentCurrent:
                                    if callable(f):
                                        f(self, current)
                            elif callable(self.cbFilamentCurrent):
                                self.cbFilamentCurrent(self, current)
                        self.internal__signalCondition("af", current )
                    else:
                        channel = int(msg[1])
                        current = int(msg[3:])
                        current = float(current) / 10
                        if self.cbCurrent:
                            if type(self.cbCurrent) is list:
                                for f in self.cbCurrent:
                                    if callable(f):
                                        f(self, channel, current)
                            elif callable(self.cbCurrent):
                                self.cbCurrent(self, channel, current)

                        self.internal__signalCondition("a{}".format(channel), current )
                except ValueError:
                    pass
            elif msg[0:len("psustate")] == "psustate":
                states = []
                for i in range(4):
                    if msg[len("psustate") + i] == '-':
                        states.append("off")
                    elif msg[len("psustate") + i] == 'C':
                        states.append("current")
                    else:
                        states.append("voltage")
                if self.cbPSUMode:
                    if type(self.cbPSUMode) is list:
                        for f in self.cbPSUMode:
                            if callable(f):
                                f(self, states)
                    elif callable(self.cbPSUMode):
                        self.cbPSUMode(self, states)
                self.internal__signalCondition("psustate", states )
            elif msg[0:len("filseta:")] == "filseta:":
                parts = msg.split(":")
                if parts[1] == "disabled":
                    if self.cbFilamentCurrentSet:
                        if type(self.cbFilamentCurrentSet) is list:
                            for f in self.cbFilamentCurrentSet:
                                if callable(f):
                                    f(self, None)
                        elif callable(self.cbFilamentCurrentSet):
                            self.cbFilamentCurrentSet(self, None)
                    self.internal__signalCondition("filseta", True )
                else:
                    try:
                        newSetValue = int(parts[1])
                        if self.cbFilamentCurrentSet:
                            if type(self.cbFilamentCurrentSet) is list:
                                for f in self.cbFilamentCurrentSet:
                                    if callable(f):
                                        f(self, newSetValue)
                            elif callable(self.cbFilamentCurrentSet):
                                self.cbFilamentCurrentSet(self, newSetValue)
                        self.internal__signalCondition("filseta", newSetValue)
                    except ValueError:
                        pass
            elif msg[0:len("seta:")] == "seta:":
                parts = msg.split(":")
                if parts[1] == "disabled":
                    if self.cbFilamentCurrentSet:
                        if type(self.cbFilamentCurrentSet) is list:
                            for f in self.cbFilamentCurrentSet:
                                if callable(f):
                                    f(self, None)
                        elif callable(self.cbFilamentCurrentSet):
                            self.cbFilamentCurrentSet(self, None)
                    self.internal__signalCondition("filseta", True )
                else:
                    try:
                        newSetValue = int(parts[1]) / 10.0
                        if self.cbFilamentCurrentSet:
                            if type(self.cbFilamentCurrentSet) is list:
                                for f in self.cbFilamentCurrentSet:
                                    if callable(f):
                                        f(self, newSetValue)
                            elif callable(self.cbFilamentCurrentSet):
                                self.cbFilamentCurrentSet(self, newSetValue)
                        self.internal__signalCondition("filseta", newSetValue)
                    except ValueError:
                        pass
            elif msg[0:len("off")] == "off":
                if self.cbOff:
                    if type(self.cbOff) is list:
                        for f in self.cbOff:
                            if callable(f):
                                f(self)
                    elif callable(self.cbOff):
                        self.cbBeamon(self)

                self.internal__signalCondition("off", True)
            elif msg[0:len("ra:")] == "ra:":
                try:
                    current = float(msg[3:])
                    if self.cbFilamentCurrent:
                        if type(self.cbFilamentCurrent) is list:
                            for f in self.cbFilamentCurrent:
                                if callable(f):
                                    f(self, current)
                        elif callable(self.cbFilamentCurrent):
                            self.cbFilamentCurrent(self, current)
                    self.internal__signalCondition("af", current )
                except ValueError:
                    pass
            elif msg[0:len("adc0:")] == "adc0:":
                # ToDo
                pass
            
            else:
                print("Unknown message {}".format(msg))
        except Exception as e:
            print(f"Failed to handle message {msg}: {e}")


    def communicationThreadMain(self):
        try:
            while True:
                c = self.port.read(1)
                self.bufferInput.push(c)

                # Check if we have a full message

                while True:
                    # First a message has to be more than 4 bytes (due to sync pattern and stop pattern)
                    if self.bufferInput.available() < 4:
                        break

                    # And we scan for the sync pattern
                    if (self.bufferInput.peek(0) == b'$') and (self.bufferInput.peek(1) == b'$') and (self.bufferInput.peek(2) == b'$'):
                        break

                    self.bufferInput.discard(1)

                # Continue waiting for message
                if self.bufferInput.available() < 4:
                    continue

                # If we see a full message we also see the terminating linefeed
                for i in range(self.bufferInput.available()):
                    if self.bufferInput.peek(i) == b'\n':
                        msg = self.bufferInput.read(i)
                        self.communicationThreadMain_HandleMessage(msg)
                        break
                    if (i > 2) and (self.bufferInput.peek(i) == b'$'):
                        # We have seen another sync pattern symbol - we discard the previous message and restart ...
                        self.bufferInput.discard(i-1)
                        break
        except serial.serialutil.SerialException:
            # Shutting down works via this exception
            pass
        except:
            pass

    def id(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        self.port.write(b'$$$id\n')
        self._lastcommand = b'$$$id\n'
        if sync:
            return self.internal__waitForMessageFilter("id")
        else:
            return None

    def getPSUVoltage(self, channel, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        cmd = False
        if channel == 1:
            cmd = b'$$$psugetv1\n'
        elif channel == 2:
            cmd = b'$$$psugetv2\n'
        elif channel == 3:
            cmd = b'$$$psugetv3\n'
        elif channel == 4:
            cmd = b'$$$psugetv4\n'
        else:
            raise ElectronGunInvalidParameterException("Power supply channel has to be in range 1 to 4")
        self.port.write(cmd)
        self._lastcommand = cmd
        if sync:
            res = self.internal__waitForMessageFilter("v{}".format(channel))
            return res
        else:
            return None

    def quakEstimateBeamCurrent(self):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")

        # Power supplies:
        #   1   Cathode
        #   2   Whenelt
        #   3   Focus
        #   4   unused
        currentCathode = self.getPSUCurrent(1, sync = True)
        currentWhenelt = self.getPSUCurrent(2, sync = True)
        currentFocus = self.getPSUCurrent(3, sync = True)

        return currentCathode - currentWhenelt - currentFocus

    def getPSUCurrent(self, channel, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        cmd = False
        if channel == 1:
            cmd = b'$$$psugeta1\n'
        elif channel == 2:
            cmd = b'$$$psugeta2\n'
        elif channel == 3:
            cmd = b'$$$psugeta3\n'
        elif channel == 4:
            cmd = b'$$$psugeta4\n'
        else:
            raiseElectronGunInvalidParameterException("Power supply channel has to be in range 1 to 4")
        self.port.write(cmd)
        self._lastcommand = cmd

        if sync:
            res = self.internal__waitForMessageFilter("a{}".format(channel))
            return res
        else:
            return None

    def getPSUModes(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        cmd = b'$$$psumode\n'
        self.port.write(cmd)
        self._lastcommand = cmd
        if sync:
            return self.internal__waitForMessageFilter("psustate")
        else:
            return None

    def getFilamentCurrent(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        cmd = b'$$$fila\n'
        self.port.write(cmd)
        self._lastcommand = cmd
        if sync:
            return self.internal__waitForMessageFilter("af")
        else:
            return None

    def off(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        cmd = b'$$$off\n'
        self.port.write(cmd)
        self._lastcommand = cmd
        # Here we add a small delay to allow the serial buffer to be
        # fully flushed before we terminate our process so we can make sure
        # we really transmit the off condition

        if sync:
            self.internal__waitForMessageFilter("off")

        time.sleep(2)

    def noprotection(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        cmd = b'$$$noprotection\n'
        self.port.write(cmd)
        self._lastcommand = cmd

    def setPSUPolarity(self, channel, polarity, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        cmd = False
        if channel == 1:
            cmd = b'$$$psupol1'
        elif channel == 2:
            cmd = b'$$$psupol2'
        elif channel == 3:
            cmd = b'$$$psupol3'
        elif channel == 4:
            cmd = b'$$$psupol4'
        else:
            raiseElectronGunInvalidParameterException("Power supply channel has to be in range 1 to 4")

        if (polarity != self.POLARITY_NEG) and (polarity != self.POLARITY_POS):
            raiseElectronGunInvalidParameterException("Polarity has to be POLARITY_NEG or POLARITY_POS")

        cmd = cmd + polarity + b'\n'
        self.port.write(cmd)
        self._lastcommand = cmd

        if sync:
            time.sleep(20)

    def setPSUVoltage(self, channel, voltage, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")

        cmd = b'$$$psusetv'

        try:
            channel = int(channel)
        except ValueError:
            raise ElectronGunInvalidParameterException("Channel has to be an integer in range 1 to 4")
        if (channel < 1) or (channel > 4):
            raise ElectronGunInvalidParameterException("Channel has to be an integer in range 1 to 4")

        try:
            voltage = int(voltage)
        except ValueError:
            raise ElectronGunInvalidParameterException("Voltage has to be integer value")

        if (voltage < 0) or (voltage > 3250):
            raise ElectronGunInvalidParameterException("Voltage has to be in range from 0 to 3250V")

        cmd = cmd + bytes(str(channel), encoding="ascii") + bytes(str(voltage), encoding="ascii")

        cmd = cmd + b'\n'
        self.port.write(cmd)
        self._lastcommand = cmd

        if self.stabilizationDelay and sync:
            time.sleep(self.stabilizationDelay)

    def setPSUCurrentMicroamps(self, channel, current, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")

        cmd = b'$$$psuseta'

        try:
            channel = int(channel)
        except ValueError:
            raise ElectronGunInvalidParameterException("Channel has to be an integer in range 1 to 4")
        if (channel < 1) or (channel > 4):
            raise ElectronGunInvalidParameterException("Channel has to be an integer in range 1 to 4")

        try:
            current = int(current)
        except ValueError:
            raise ElectronGunInvalidParameterException("Current has to be integer value")

        if (current < 0) or (current > 2000):
            raise ElectronGunInvalidParameterException("Current has to be in range from 0 to 2000 uA")

        cmd = cmd + bytes(str(channel), encoding="ascii") + bytes(str(voltage), encoding="ascii")

        cmd = cmd + b'\n'
        self.port.write(cmd)
        self._lastcommand = cmd

        if self.stabilizationDelay and sync:
            time.sleep(self.stabilizationDelay)


    def setFilamentCurrent(self, currentMa, *ignore, sync = False, noprotection = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")

        if noprotection:
            if currentMa < 0:
                raise ElectronGunInvalidParameterException("Filament current has to be an integer in range 0 to 100 mA (in 100 uA steps)")
        else:
            if (currentMa < 0) or (currentMa > 100):
                raise ElectronGunInvalidParameterException("Filament current has to be an integer in range 0 to 100 mA (in 100 uA steps)")
        try:
            currentMa = int(currentMa)
        except ValueError:
            raise ElectronGunInvalidParameterException("Filament current has to be an integer in range 0 to 100 mA (in 100 uA steps)")

        cmd = b'$$$setfila' + bytes(str(currentMa*10), encoding="ascii") + b'\n'
        self.port.write(cmd)
        self._lastcommand = cmd

        if sync:
            return self.internal__waitForMessageFilter("filseta")
        else:
            return None

    def setFilamentOn(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")

        cmd = b'$$$filon\n'
        self.port.write(cmd)
        self._lastcommand = cmd

        if sync:
            return self.internal__waitForMessageFilter("filseta")

    def setFilamentOff(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")

        cmd = b'$$$filoff\n'
        self.port.write(cmd)
        self._lastcommand = cmd

        if sync:
            while True:
                res = self.internal__waitForMessageFilter("filseta")
                if res == "disabled":
                    return res
                try:
                    if float(res) == 0:
                        return res
                except ValueError:
                    pass

    def runInsulationTest(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        self.port.write(b'$$$insul\n')
        self._lastcommand = b'$$$insul\n'
        if sync:
            return self.internal__waitForMessageFilter("insulok")
        else:
            return None

    def beamOn(self, *ignore, filament_current = None, sync = False):
        if filament_current is not None:
            egun.setFilamentOff(sync = sync)
            egun.setFilamentCurrent(filament_current, sync = snyc)
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        self.port.write(b'$$$beamon\n')
        self._lastcommand = b'$$$beamon\n'
        if sync:
            return self.internal__waitForMessageFilter("beamon")
        else:
            return None

    def reset(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        self.port.write(b'$$$reset\n')
        self._lastcommand = b'$$$reset\n'
        if sync:
            time.sleep(10)
            self.port.write(b'$$$id\n')
            self._lastcommand = b'$$$id\n'
            if sync:
                return self.internal__waitForMessageFilter("id")
            else:
                return None

    def jabber(self, *ignore, sync = False):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        self.port.write(b'$$$$$$$$$$$$id\n')
        if sync:
            return self.internal__waitForMessageFilter("id")
        else:
            return None

    def get_status(self):
        # Synchronous fetching of system status ...

        state = {}

        state['pylib'] = egun_version
        state['id'] = self.id(sync = True)
        state['hv_voltage'] = [ ]
        state['hv_current'] = [ ]
        for psuidx in [ 1, 2, 3, 4 ]:
            state['hv_voltage'].append(self.getPSUVoltage(psuidx, sync = True))
            state['hv_current'].append(self.getPSUCurrent(psuidx, sync = True))
        state['hv_modes'] = self.getPSUModes(sync = True)
        state['filament_current'] = self.getFilamentCurrent(sync = True)

        return state

    def __str__(self):
        state = self.get_status()

        s = f"Electron Gun:\n"
        s += f"\tPyLib version:\t{egun_version}\n"
        s += f"\tController version: {state['id']['version']} (rev. {state['id']['revision']})\n"
        s += f"\tHigh voltage supplies:\n"
        s += f"\t\tCathode:\t\t{state['hv_voltage'][1]} V, {state['hv_current'][1] / 10.0} uA (mode {state['hv_modes'][1]})\n"
        s += f"\t\tWhenelt cylinder:\t{state['hv_voltage'][0]} V, {state['hv_current'][0] / 10.0} uA (mode {state['hv_modes'][0]})\n"
        s += f"\t\tFocus voltage:\t{state['hv_voltage'][2]} V, {state['hv_current'][2] / 10.0} uA (mode {state['hv_modes'][2]})\n"
        s += f"\t\tAux supply:\t\t{state['hv_voltage'][3]} V, {state['hv_current'][3] / 10.0} uA (mode {state['hv_modes'][3]})\n"
        s += f"\tFilament current:\n"
        s += f"\t\tMeasured (high side controller): {state['filament_current']}\n"

        return s

    def __repr__(self):
        return self.get_status()
