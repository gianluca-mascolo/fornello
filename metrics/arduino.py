#!/usr/bin/env python3
import json
import pickle
import socket
import struct
import time

import requests
import serial
from pynput import keyboard

loki_url = "http://localhost:3100/loki/api/v1/push"
CARBON_SERVER = "localhost"
CARBON_PORT = 2004
keypress = [None]


def on_press(key):
    if key == keyboard.Key.esc:
        keypress[0] = "ESC"


def sendlog(msg: str, timestamp: int):
    headers = {"Content-type": "application/json"}

    labels = {"source": "serialport", "job": "serialcollector", "host": "arduino"}
    payload = {"streams": [{"stream": {"source": "serialport"}, "values": [[str(timestamp), msg]]}]}
    r = requests.post(loki_url, data=json.dumps(payload), headers=headers)
    if r.status_code >= 200 or r.status_code <= 299:
        return True
    else:
        return False


def main():
    listener = keyboard.Listener(on_press=on_press)
    listener.start()

    ser = serial.Serial()
    ser.baudrate = 9600
    ser.port = "/dev/ttyACM0"
    ser.timeout = 5
    ser.open()
    while not ser.is_open:
        print("Waiting for serial port to be open")

    serial_ready = False
    serial_wait = 10
    sample = 0
    basestamp = time.time_ns()
    ardunostart = -1
    while ser.is_open:
        rbytes = ser.read_until()
        serial_line = rbytes.decode("ascii").rstrip()
        if serial_line == "READY":
            serial_ready = True
            continue
        if serial_ready:
            print(f"{sample} {serial_line}")
            metrics = {}
            for m in serial_line.split(","):
                metric = m.split(":")
                if len(metric) == 2:
                    metrics = metrics | {metric[0]: metric[1]}
            # print(metrics)
            if sample == 0:
                arduinostart = int(metrics["time"])
            timestamp = basestamp + (int(metrics["time"]) - arduinostart) * 1000000
            listOfMetricTuples = [(f"arduino.fornello.{k}", (int(timestamp / 1000000000), float(v))) for k, v in metrics.items() if k != "time"]
            payload = pickle.dumps(listOfMetricTuples, protocol=2)
            header = struct.pack("!L", len(payload))
            message = header + payload
            sock = socket.socket()
            sock.connect((CARBON_SERVER, CARBON_PORT))
            s = sock.sendall(message)
            sock.close()
            sendlog(serial_line, timestamp)
            sample += 1
        else:
            serial_wait -= 1
        if keypress[0] == "ESC" or serial_wait <= 0:
            break
    ser.close()
    listener.stop()
    return True


if __name__ == "__main__":
    main()
