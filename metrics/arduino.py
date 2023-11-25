#!/usr/bin/env python3
import json
import pickle
import signal
import socket
import struct
import sys
import time

from requests import post
from serial import Serial, SerialException

LOKI_URL = "http://localhost:3100/loki/api/v1/push"
CARBON_SERVER = "localhost"
CARBON_PORT = 2004
SERIAL_SETTINGS = {"baudrate": 9600, "port": "/dev/ttyACM0", "timeout": 5}


class SignalHandler:
    def __init__(self):
        self.received = False

    def setup(self):
        signal.signal(signal.SIGTERM, self.catch)
        signal.signal(signal.SIGINT, self.catch)

    def catch(self, signum, frame):
        self.received = True


class ArduinoLogger:
    def __init__(self, metric_path: str):
        self.ready = False
        self.sample = -1
        self.basestamp = time.time_ns()
        self.arduinostamp = 0
        self.metrics = []
        self.metric_path = metric_path

    def readline(self, ser: Serial) -> str:
        try:
            read_bytes = ser.read_until()
            decoded_line = read_bytes.decode("ascii").rstrip()
            self.metrics = self.__metrics(decoded_line)
        except Exception:
            return ""
        if self.ready:
            self.sample += 1
        return decoded_line

    def setup(self, ser: Serial, retry: int) -> bool:
        msg = ""
        while retry > 0 and msg != "READY":
            print("Logging is not ready", file=sys.stderr)
            msg = self.readline(ser)
            retry -= 1
        if msg == "READY":
            for m in self.readline(ser).split(","):
                metric = m.split(":")
                if len(metric) == 2 and metric[0] == "time":
                    self.arduinostamp = int(metric[1])
                    self.ready = True
                    self.basestamp = time.time_ns()
            return self.ready
        else:
            return False

    def __time(self, millis) -> int:
        return int((self.basestamp + (int(millis) - self.arduinostamp) * 1000000) / 1000000000)

    def __metrics(self, msg: str) -> list:
        mdict = {}
        for m in msg.split(","):
            metric = m.split(":")
            if len(metric) == 2:
                mdict = mdict | {metric[0]: metric[1]}
        if "time" in mdict:
            return [(f"{self.metric_path}.{k}", (self.__time(mdict["time"]), float(v))) for k, v in mdict.items() if k != "time"]
        else:
            return []


def send_logs(msg: str, src: str, url: str) -> bool:
    headers = {"Content-type": "application/json"}
    payload = {"streams": [{"stream": {"source": src}, "values": [[str(time.time_ns()), msg]]}]}
    try:
        r = post(url, data=json.dumps(payload), headers=headers)
    except Exception:
        return False
    if r.status_code >= 200 or r.status_code <= 299:
        return True
    else:
        return False


def send_metrics(metrics: list, server: str, port: int) -> bool:
    payload = pickle.dumps(metrics, protocol=2)
    header = struct.pack("!L", len(payload))
    message = header + payload
    try:
        sock = socket.socket()
        sock.connect((server, port))
        sock.sendall(message)
        sock.close()
    except Exception:
        return False
    return True


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
        if terminate.received:
            sys.exit(1)
        time.sleep(1)

    log = ArduinoLogger("arduino.fornello")
    if not log.setup(ser=ser, retry=30):
        print("Error: Arduino setup failed", file=sys.stderr)
        ser.close()
        sys.exit(1)
    while ser.is_open and not terminate.received:
        log_line = log.readline(ser)
        print(f"{log.sample} {log_line}")
        if not send_metrics(log.metrics, CARBON_SERVER, CARBON_PORT):
            print("Warning: Failed to send metrics", file=sys.stderr)
        if not send_logs(msg=log_line, src="serialport", url=LOKI_URL):
            print("Warning: Failed to send serialport logs", file=sys.stderr)
    ser.close()
    sys.exit(0)

if __name__ == "__main__":
    main()
