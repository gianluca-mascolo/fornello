#!/usr/bin/env python3
import json
import pickle
import socket
import struct
import time
import signal
import requests
import sys
import serial
from serial import SerialException, Serial

loki_url = "http://localhost:3100/loki/api/v1/push"
CARBON_SERVER = "localhost"
CARBON_PORT = 2004

SERIAL_SETTINGS = {'baudrate': 9600, 'port': '/dev/ttyACM0', 'timeout': 5}

class SignalHandler:
    def __init__(self):
        self.received = False
    def setup(self):
        signal.signal(signal.SIGTERM, self.catch)
        signal.signal(signal.SIGINT, self.catch)
    def catch(self, signum, frame):
        self.received = True
        return True

class ArduinoLogger:
    def __init__(self):
        self.ready = False
        self.sample = -1
        self.basestamp = time.time_ns()
        self.arduinostamp = 0
    def readline(self, ser: Serial):
        try:
            read_bytes = ser.read_until()
            decoded_line = read_bytes.decode("ascii").rstrip()
        except:
            return ''
        if self.ready:
            self.sample += 1
        return decoded_line
    def setup(self, ser: Serial, retry: int):
        msg = ''
        while retry > 0 and msg != 'READY':
            print("Waiting READY line")
            msg = self.readline(ser)
            retry -= 1
        if msg == 'READY':
            print("Arduino is READY")
            for m in self.readline(ser).split(','):
                metric = m.split(":")
                if len(metric) == 2 and metric[0] == 'time':
                    self.arduinostamp = int(metric[1])
                    self.ready = True
                    self.basestamp = time.time_ns()
            return self.ready
        else:
            return False
    def time_ns(self):
        return True


def sendlog(msg: str, timestamp: int):
    headers = {"Content-type": "application/json"}

    labels = {"source": "serialport", "job": "serialcollector", "host": "arduino"}
    payload = {"streams": [{"stream": {"source": "serialport"}, "values": [[str(timestamp), msg]]}]}
    try:
        r = requests.post(loki_url, data=json.dumps(payload), headers=headers)
    except:
        return False
    if r.status_code >= 200 or r.status_code <= 299:
        return True
    else:
        return False


def main():
    terminate = SignalHandler()
    terminate.setup()
    try:
        ser = Serial(**SERIAL_SETTINGS)
    except SerialException as e:
        print(f"Serial Error: {e}")
        sys.exit(1)
    except ValueError as ve:
        print(f"Error: {ve}")
        sys.exit(1)

    while not ser.is_open:
        print("Waiting for serial port to be open")
        time.sleep(1)
        if terminate.received:
            sys.exit(1)

    message = ArduinoLogger()
    if not message.setup(ser=ser,retry=10):
        print("Error: Arduino setup failed")
        sys.exit(1)
    while ser.is_open:
        serial_line = message.readline(ser)
        print(f"{message.sample} {serial_line}")
        metrics = {}
        for m in serial_line.split(","):
            metric = m.split(":")
            if len(metric) == 2:
                metrics = metrics | {metric[0]: metric[1]}
        timestamp = message.basestamp + (int(metrics["time"]) - message.arduinostamp) * 1000000
        listOfMetricTuples = [(f"arduino.fornello.{k}", (int(timestamp / 1000000000), float(v))) for k, v in metrics.items() if k != "time"]
        payload = pickle.dumps(listOfMetricTuples, protocol=2)
        header = struct.pack("!L", len(payload))
        message1 = header + payload
        try:
            sock = socket.socket()
            sock.connect((CARBON_SERVER, CARBON_PORT))
            s = sock.sendall(message1)
            sock.close()
        except:
            pass
        sendlog(serial_line, timestamp)
        if terminate.received:
            break
    ser.close()
    return True


if __name__ == "__main__":
    main()
