import serial
import threading
import time

from collections import deque

class ElectronGunRingbuffer:
    def __init__(self, bufferSize = 512):
        self.bufferSize = bufferSize
        self.buffer = [ None ] * bufferSize
        self.head = 0
        self.tail = 0
        self.lock = threading.Lock()

    def isAvailable(self, *ignore, blocking = True):
        if blocking:
            self.lock.acquire()
        if self.head == self.tail:
            if blocking:
                self.lock.release()
            return True
        else:
            if blocking:
                self.lock.release()
            return False

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
        res = self.bufferSize - self.available(blocking = False)
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
            if self.remainingCapacity(blocking = False) == 0:
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
        ret = [ None ] * len
        for i in range(len):
            ret[i] = self.buffer[(self.tail + i) % self.bufferSize]
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

    def __init__(self, portFile = '/dev/ttyU0'):
        self.bufferInput = ElectronGunRingbuffer()

        self.port = False
        self.portFile = portFile
        self.port = serial.Serial(portFile, baudrate=19200, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, timeout=30)
        self.thrProcessing = threading.Thread(target=self.communicationThreadMain)
        self.thrProcessing.start()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        if self.port:
            self.port.close()
            self.port = False
        if self.thrProcessing:
            self.thrProcessing.join()
            self.thrProcessing = False

    def close(self):
        if self.port:
            self.port.close()
            self.port = False
        if self.thrProcessing:
            self.thrProcessing.join()
            self.thrProcessing = False

    def communicationThreadMain_HandleMessage(self, msg):
        print(msg)

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
                        print("Reading {} bytes".format(i))
                        msg = self.bufferInput.read(i)
                        print(msg)
                        self.communicationThreadMain_HandleMessage(msg)
                        break
        except serial.serialutil.SerialException:
            # Shutting down works via this exception
            pass
        except:
            pass

    def id(self):
        if self.port == False:
            raise ElectronGunNotConnected("Electron gun currently not connected")
        self.port.write(b'$$$id\n')

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


if __name__ == "__main__":
    with ElectronGunControl() as ctrl:
        time.sleep(10)
        print("Sending ID")
        ctrl.id()
        time.sleep(10)
        ctrl.getPSUCurrent(1)
        time.sleep(10)
        ctrl.getPSUCurrent(2)
        time.sleep(10)
