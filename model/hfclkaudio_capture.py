#!/usr/bin/env python3

# Capture HFCLKAUDIO frequency step response using a Rigol DS1102E oscilloscope.
# Use CLKCONFIG.BYPASS to output raw HFCLKAUDIO on MCLK. Apply a 1000 step input
# periodically and simultaneously toggle a GPIO. Connect MCLK to channel 1 and
# the trigger GPIO to the external trigger. Run this script to capture many step
# response samples and then use hfclkaudio_dynamics.ipynb to combine them to
# reduce noise.
#
# Based on https://github.com/dstahlke/rigol_long_mem/blob/master/rigol.py

import os
import time
import numpy as np
from enum import Enum


class USBTMC:
    """Simple implementation of a USBTMC device driver, in the style of visa.h"""

    def __init__(self, device):
        self.device = device
        self.file = os.open(device, os.O_RDWR)

    def write(self, command: str):
        os.write(self.file, command.encode(encoding="ascii"))
        # The Rigol docs say to wait a bit after each command.
        # time.sleep(0.1)

    def read(self, length: int = 4000):
        return os.read(self.file, length)

    def ask(self, command: str, length: int = 4000) -> str:
        self.write(command)
        return self.read(length).decode("ascii")

    def ask_float(self, command):
        return float(self.ask(command))

    def name(self):
        return self.ask("*IDN?")

    def reset(self):
        self.write("*RST")


class RigolScope(USBTMC):
    """Class to control a Rigol DS1000E/D series oscilloscope"""

    def __init__(self, device):
        super().__init__(device)

    def _channel(self, c: int) -> str:
        if c < 0 or c > 2:
            raise Exception(f"Channel {c} out of range")
        return f"CHAN{c}"

    def set_channel_display(self, channel: int, enabled: bool):
        on_off = "ON" if enabled else "OFF"
        self.write(f":{self._channel(channel)}:DISP {on_off}")

    def vertical_scale(self, channel: int):
        return self.ask_float(f":{self._channel(channel)}:SCAL?")

    def set_vertical_scale(self, channel: int, scale: float):
        return self.write(f":{self._channel(channel)}:SCAL {scale:.12f}")

    def vertical_offset(self, channel: int):
        return self.ask_float(f":{self._channel(channel)}:OFFS?")

    def set_vertical_offset(self, channel: int, offset: float):
        return self.write(f":{self._channel(channel)}:OFFS {offset:.12f}")

    def time_scale(self):
        return self.ask_float(":TIM:SCAL?")

    def set_time_scale(self, scale: float):
        return self.write(f":TIM:SCAL {scale:.12f}")

    def time_offset(self):
        return self.ask_float(":TIM:OFFS?")

    def set_time_offset(self, offset: float):
        return self.write(f":TIM:OFFS {offset:.12f}")

    def sample_rate(self):
        return self.ask_float(":ACQ:SAMP?")

    def trigger(self):
        self.write(":RUN")
        self.wait_for_stop()
        self.write(":STOP")

    def wait_for_stop(self):
        while self.ask(":TRIG:STAT?") != "STOP":
            time.sleep(0.5)
        # A little extra delay, just in case.
        time.sleep(0.5)

    def _wave_raw(self, chan=1) -> np.ndarray:
        self.write(f":WAV:DATA? {self._channel(chan)}")
        # First 10 bytes are header.
        x = self.read(2000000)[10:]
        return np.frombuffer(x, "B")

    def wave(self, chan=1) -> np.ndarray:
        raw_byte = self._wave_raw(chan)
        volts_div = self.vertical_scale(chan)
        vert_offset = self.vertical_offset(chan)
        # This equation is from "DS1000E-D Data Format.pdf"
        wave = (240 - raw_byte) * (volts_div / 25.0) - (vert_offset + volts_div * 4.6)
        return wave

    def wave_time(self, nsamps) -> np.ndarray:
        # This equation is from "DS1000E-D Data Format.pdf".  It only works for 600 samples.
        pt_num = np.arange(nsamps)
        return (pt_num - nsamps / 2.0) / self.sample_rate() + self.time_offset()

    def local_mode(self):
        self.write(":KEY:FORC")


def main():
    scope = RigolScope("/dev/usbtmc0")
    print(scope.name())

    scope.write(":STOP")

    scope.write(":DISP:MNUS OFF")

    scope.set_channel_display(1, True)
    scope.set_channel_display(2, False)

    # Configure scaling
    scope.set_vertical_scale(1, 430e-3)
    scope.set_vertical_offset(1, -1.58)
    scope.set_time_scale(20e-9)
    # Set offset so trigger is near beginning of buffer. Not at the very
    # beginning (8192ns) because the first few samples seem to be garbage.
    scope.set_time_offset(8000e-9)

    # Configure trigger
    scope.write(":TRIG:MODE EDGE")
    scope.write(":TRIG:EDGE:SOUR EXT")
    scope.write(":TRIG:EDGE:SLOPE POS")
    scope.write(":TRIG:EDGE:LEV 1.2")
    scope.write(":TRIG:EDGE:SWE SING")

    scope.write(":WAV:POIN:MODE RAW")
    scope.write(":ACQ:TYPE NORM")
    scope.write(":ACQ:MODE REAL_TIME")
    scope.write(":ACQ:MEMD NORMAL")

    # Make sure we are using 1 GSa/s
    assert scope.sample_rate() == 1e9

    i = 0
    while True:
        scope.trigger()
        volts = scope.wave()
        time = scope.wave_time(volts.shape[0])
        data = np.stack((time, volts))
        np.save(f"data/hclkaudio_step/step_100_{i}.npy", data)
        i += 1


if __name__ == "__main__":
    main()
