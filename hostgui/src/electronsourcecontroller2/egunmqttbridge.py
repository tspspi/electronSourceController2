
import argparse
import sys
import logging
import time
import threading

sys.path.append("/home/tsp/githubRepos/electronSourceController2/hostgui/src/")

import json

import signal, lockfile, grp, os

from pwd import getpwnam
from daemonize import Daemonize

import paho.mqtt.client as mqtt

# import mqttpattern
import mqttpattern
from eguncom import ElectronGunControl

class EGunMQTTDaemon:
    def __init__(self, args, logger):
        self.args = args
        self.logger = logger
        self.terminate = False
        self.rereadConfig = True
        self.configuration = None

        self.egun = None
        self.egunReopenCounter = 10

        self.mqtt = None

        self._mqttHandlers = None

        self.threadNotify = threading.Event()
        self.threadLock = threading.Lock()
        self.commandQueue = []

    def signalSigHup(self, *args):
        self.rereadConfig = True

    def signalTerm(self, *args):
        self.terminate = True

    def __enter__(self):
        return self

    def __exit__(self, type, value, tb):
        pass

    def run(self):
        signal.signal(signal.SIGHUP, self.signalSigHup)
        signal.signal(signal.SIGTERM, self.signalTerm)
        signal.signal(signal.SIGINT, self.signalTerm)

        self.logger.info("Service running")

        while True:
            if self.rereadConfig:
                self._mqtt_publish("config/reload")

                self.rereadConfig = False

                newConfiguration = None
                try:
                    with open(self.args.config, 'r') as cfgFile:
                        newConfiguration = json.load(cfgFile)
                    self.logger.debug(f"Loaded new configuration file {self.args.config}")
                except FileNotFoundError:
                    self.logger.error(f"Failed to open configuration file {self.args.config}")
                    continue
                except json.decoder.JSONDecodeError as e:
                    self.logger.error(f"JSON Decode error: {e}")
                    continue

                # Check our required configuration fields are present ...
                try:
                    if not ('egun' in newConfiguration):
                        self.logger.error("Missing egun configuration in configuration file")
                        continue
                    if not ('port' in newConfiguration['egun']):
                        self.logger.error("Missing egun port configuration in configuration file")
                        continue
                    if not ('mqtt' in newConfiguration):
                        self.logger.error("Missing MQTT configuration in configuration file")
                        continue
                    if not ('broker' in newConfiguration['mqtt']):
                        self.logger.error("Missing broker configuration in configuration file")
                        continue
                    if not ('user' in newConfiguration['mqtt']):
                        self.logger.error("Missing user configuration in configuration file")
                        continue
                    if not ('password' in newConfiguration['mqtt']):
                        self.logger.error("Missing password configuration in configuration file")
                        continue
                    if not ('basetopic' in newConfiguration['mqtt']):
                        self.logger.error("Missing base topic configuration in configuration file")
                        continue

                    if len(newConfiguration['mqtt']['basetopic']) < 1:
                        self.logger.error("New base topic is empty ...")
                        continue

                    # Format conversions
                    try:
                        newConfiguration['mqtt']['port'] = int(newConfiguration['mqtt']['port'])
                        if (newConfiguration['mqtt']['port'] < 1) or (newConfiguration['mqtt']['port'] > 65535):
                            self.logger.error("MQTT port {newConfiguration['mqtt']['port']} is invalid")
                            continue
                    except ValueError:
                        self.logger.error("MQTT port {newConfiguration['mqtt']['port']} is invalid")
                        continue
                except Exception as e:
                    self.logger.error(f"Unknown error while processing configuration {e}")
                    continue

                if newConfiguration['mqtt']['basetopic'][-1] != '/':
                    newConfiguration['mqtt']['basetopic'] = newConfiguration['mqtt']['basetopic'] + "/"
                    self.logger.warning("Base topic not ending in trailing slash /. Appending")

                # Update configuration
                self.configuration = newConfiguration
                self.egunReopenCounter = 1

                self._mqtt_publish("config/reloaded")

                # Close and re-create MQTT client
                if self.mqtt:
                    self.mqtt.disconnect()
                    self.mqtt = None
                self.mqtt = mqtt.Client()
                self.mqtt.on_connect = self._mqtt_on_connect
                self.mqtt.on_message = self._mqtt_on_message
                self.mqtt.username_pw_set(self.configuration['mqtt']['user'], self.configuration['mqtt']['password'])
                self.mqtt.connect(self.configuration['mqtt']['broker'], self.configuration['mqtt']['port'])
                self.mqtt.loop_start()

                # self.egun = ElectronGunControl(self.configuration['egun'])

            if self.terminate:
                break

            if not self.egun:
                self.egunReopenCounter = self.egunReopenCounter - 1
                if self.egunReopenCounter == 0:
                    self._mqtt_publish("egun/reconnecting", { 'port' : self.configuration['egun']['port'] })
                    self.egunReopenCounter = 10

                    if not self.configuration:
                        self.logger.error("Cannot open egun, no configuration loaded")
                    else:
                        # Re-open egun (or try to ...)
                        try:
                            self.logger.debug(f"Trying to reconnect to electron gun at {self.configuration['egun']['port']}")
                            self.egun = ElectronGunControl(portFile = self.configuration['egun']['port'])

                            self.egun.cbIdentify = [ self._egunCallback_Identify ]
                            #self.egun.cbInsulation = lambda c,isOk,failedList : print("Insulation is {}, failed list: {}".format(isOk, failedList))
                            self.egun.cbVoltage = [ self._egunCallback_Voltage ]
                            self.egun.cbCurrent = [ self._egunCallback_Current ]
                            #self.egun.cbPSUMode = lambda c,modes : print("Power supply modes: {}".format(modes))
                            #self.egun.cbFilamentCurrent = lambda c,current : print("Filament current: {}".format(current))
                            #self.egun.cbFilamentCurrentSet = lambda c,current : print("Filament current set to {}".format(current))
                            #self.egun.cbBeamon = lambda c : print("Beam on ...")
                            time.sleep(10)
                            ret = self.egun.id()
                            time.sleep(0.1)
                            self._mqtt_publish("egun/connected", { 'port' : self.configuration['egun']['port'] })
                        except Exception as e:
                            self.egun = None
                            self.logger.error(f"Failed to connect to electron gun: {e}")
                            self._mqtt_publish("egun/confailed", { 'port' : self.configuration['egun']['port'], 'exception' : str(e) })

            self.threadNotify.wait(1)
            if not self.threadNotify.is_set():
                self._mqtt_publish("keepalive")

            with self.threadLock:
                while len(self.commandQueue) != 0:
                    nextCmd = self.commandQueue.pop()

                    # Handle command ...
                    if nextCmd['cmd'] == 'id':
                        if not self.egun:
                            logger.error("Requested eGun ID but electron source not connected")
                            self._mqtt_publish("error", { 'message' : "Requested eGun ID but electron source not connected"})
                        else:
                            self.egun.id()
                    if nextCmd['cmd'] == 'getv':
                        if not self.egun:
                            logger.error("Requested voltage but electron source not connected")
                            self._mqtt_publish("error", { 'message' : "Electron source not connected"})
                        else:
                            self.egun.getPSUVoltage(nextCmd['channel'])
                    if nextCmd['cmd'] == 'geta':
                        if not self.egun:
                            logger.error("Requested current but electron source not connected")
                            self._mqtt_publish("error", { 'message' : "Electron source not connected"})
                        else:
                            self.egun.getPSUCurrent(nextCmd['channel'])

                    # Crude rate limiting ...
                    time.sleep(0.2)
        if self.mqtt:
            self.mqtt.disconnect()
            self.mqtt = None
        if self.egun:
            self.egun.off()
            self.egun.close()
            self.egun = None

        self.logger.info("Shutting down due to user request")

    def _egunCallback_Identify(self, egun, versionDate, versionRevision):
        self.logger.debug(f"Received ID response. Version: {versionDate}, Revision: {versionRevision}")
        self._mqtt_publish("egun/id", { 'version' : versionDate, 'revision' : versionRevision }, retain=True)

    def _egunCallback_Voltage(self, egun, channel, voltage):
        self.logger.debug(f"Received voltage response: Channel {channel}: {voltage}")
        self._mqtt_publish("egun/voltage", { 'channel' : channel, 'voltage' : voltage })

    def _egunCallback_Current(self, egun, channel, current):
        self.logger.debug(f"Received current response: Channel {channel}: {current}")
        self._mqtt_publish("egun/current", { 'channel' : channel, 'current' : current })

    def _msghandler_id_request(self, message):
        self.logger.debug("Received ID request")
        with self.threadLock:
            self.commandQueue.append({ 'cmd' : 'id' })

    def _msghandler_voltage_request(self, message):
        try:
            channel = message.payload['channel']
            if (channel < 1) or (channel > 4):
                self.logger.error(f"Received invalid voltage request for channel {channel}")
            else:
                with self.threadLock:
                    self.commandQueue.append({ 'cmd' : 'getv', 'channel' : channel })
        except:
            self.logger.error(f"Received invalid voltage request {message.topic}:{message.payload}")
            pass

    def _msghandler_current_request(self, message):
        try:
            channel = message.payload['channel']
            if (channel < 1) or (channel > 4):
                self.logger.error(f"Received invalid current request for channel {channel}")
            else:
                self.logger.debug(f"Received PSU current request for channel {channel}")
                with self.threadLock:
                    self.commandQueue.append({ 'cmd' : 'geta', 'channel' : channel })
        except:
            self.logger.error(f"Received invalid current request {message.topic}:{message.payload}")
            pass


    def _mqtt_publish(self, topic, message=None, prependBaseTopic=True, retain=False):
        if not self.mqtt:
            self.logger.warning(f"Dropping message to {topic} - no MQTT connection")
            return False

        if isinstance(message, dict):
            message = json.dumps(message, cls=CustomJSONEncoder)

        if prependBaseTopic:
            topic = self.configuration['mqtt']['basetopic'] + topic

        try:
            if not (message is None):
                self.mqtt.publish(topic, payload=message, qos=0, retain=retain)
            else:
                self.mqtt.publish(topic, qos=0, retain=retain)
            self.logger.debug(f"MQTT: Published to {topic}")
            return True
        except Exception as e:
            self.logger.error(f"MQTT publish failed: {e}")
            return False

    def _mqtt_on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.logger.debug(f"Connected to {self.configuration['mqtt']['broker']}:{self.configuration['mqtt']['port']} as {self.configuration['mqtt']['user']}")

            self._mqttHandlers = mqttpattern.MQTTPatternMatcher()
            self._mqttHandlers.registerHandler(f"{self.configuration['mqtt']['basetopic']}egun/id/request", self._msghandler_id_request)
            self._mqttHandlers.registerHandler(f"{self.configuration['mqtt']['basetopic']}egun/voltage/request", self._msghandler_voltage_request)
            self._mqttHandlers.registerHandler(f"{self.configuration['mqtt']['basetopic']}egun/current/request", self._msghandler_current_request)

            #Subscribe all messages in our basetopic
            client.subscribe(self.configuration['mqtt']['basetopic']+"#")

            self.logger.debug("Subscribed to {}".format(self.configuration['mqtt']['basetopic']+"#"))

            self._mqtt_publish("connected")
        else:
            self.logger.error(f"Failed connecting to {self.configuration['mqtt']['broker']}:{self.configuration['mqtt']['port']} as {self.configuration['mqtt']['user']}, retrying")

    def _mqtt_on_message(self, client, userdata, msg):
        #self.logger.debug(f"Received message on {msg.topic}, calling handlers")
        try:
            msg.payload = json.loads(str(msg.payload.decode('utf-8', 'ignore')))
        except:
            # Ignore if we don't have a JSON payload
            pass

        self._mqttHandlers.callHandlers(msg.topic, msg)

class CustomJSONEncoder(json.JSONEncoder):
    def default(self, obj):
        #if isinstance(obj, numpy.ndarray):
        #    return obj.tolist()
        #if isinstance(obj, datetime):
        #    return obj.__str__()
        if isinstance(obj, ElectronGunControl):
            return obj.__str__()
        return json.JSONEncoder.default(self, obj)


# Daemonization specific code
# ---------------------------

def mainDaemon():
    parg = parseArguments()
    args = parg['args']
    logger = parg['logger']

    logger.debug("Daemon starting ...")
    with EGunMQTTDaemon(args, logger) as mqttDaemon:
        mqttDaemon.run()

def parseArguments():
    ap = argparse.ArgumentParser(description = 'Example daemon')
    ap.add_argument('-f', '--foreground', action='store_true', help="Do not daemonize - stay in foreground and dump debug information to the terminal")

    ap.add_argument('--uid', type=str, required=False, default=None, help="User ID to impersonate when launching as root")
    ap.add_argument('--gid', type=str, required=False, default=None, help="Group ID to impersonate when launching as root")
    ap.add_argument('--chroot', type=str, required=False, default=None, help="Chroot directory that should be switched into")
    ap.add_argument('--pidfile', type=str, required=False, default="/var/run/egunmqtt.pid", help="PID file to keep only one daemon instance running")
    ap.add_argument('--loglevel', type=str, required=False, default="error", help="Loglevel to use (debug, info, warning, error, critical). Default: error")
    ap.add_argument('--logfile', type=str, required=False, default="/var/log/egunmqtt.log", help="Logfile that should be used as target for log messages")

    ap.add_argument('--config', type=str, required=False, default="/etc/egunmqtt.conf", help="Configuration file for MQTT bridge (default /etc/egunmqtt.conf)")

    args = ap.parse_args()
    loglvls = {
        "DEBUG"     : logging.DEBUG,
        "INFO"      : logging.INFO,
        "WARNING"   : logging.WARNING,
        "ERROR"     : logging.ERROR,
        "CRITICAL"  : logging.CRITICAL
    }
    if not args.loglevel.upper() in loglvls:
        print("Unknown log level {}".format(args.loglevel.upper()))
        sys.exit(1)

    logger = logging.getLogger()
    logger.setLevel(loglvls[args.loglevel.upper()])
    if args.logfile:
        fileHandleLog = logging.FileHandler(args.logfile)
        logger.addHandler(fileHandleLog)
    if args.foreground:
        streamHandleLog = logging.StreamHandler()
        logger.addHandler(streamHandleLog)

    return { 'args' : args, 'logger' : logger }

# Entry function for CLI program
# This also configures the daemon properties

def mainStartup():
    parg = parseArguments()
    args = parg['args']
    logger = parg['logger']

    daemonPidfile = args.pidfile
    daemonUid = None
    daemonGid = None
    daemonChroot = "/"

    if args.uid:
        try:
            args.uid = int(args.uid)
        except ValueError:
            try:
                args.uid = getpwnam(args.uid).pw_uid
            except KeyError:
                logger.critical("Unknown user {}".format(args.uid))
                print("Unknown user {}".format(args.uid))
                sys.exit(1)
        daemonUid = args.uid
    if args.gid:
        try:
            args.gid = int(args.gid)
        except ValueError:
            try:
                args.gid = grp.getgrnam(args.gid)[2]
            except KeyError:
                logger.critical("Unknown group {}".format(args.gid))
                print("Unknown group {}".format(args.gid))
                sys.exit(1)

        daemonGid = args.gid

    if args.chroot:
        if not os.path.isdir(args.chroot):
            logger.critical("Non existing chroot directors {}".format(args.chroot))
            print("Non existing chroot directors {}".format(args.chroot))
            sys.exit(1)
        daemonChroot = args.chroot

    if args.foreground:
        logger.debug("Launching in foreground")
        with EGunMQTTDaemon(args, logger) as mqttDaemon:
            mqttDaemon.run()
    else:
        logger.debug("Daemonizing ...")
        daemon = Daemonize(
            app="EgunMQTTBridge",
            action=mainDaemon,
            pid=daemonPidfile,
            user=daemonUid,
            group=daemonGid,
            chdir=daemonChroot
        )
        daemon.start()


if __name__ == "__main__":
    mainStartup()
