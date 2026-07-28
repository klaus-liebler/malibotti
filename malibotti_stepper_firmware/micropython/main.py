"""ESP32 MicroPython - zwei 28BYJ-48 Schrittmotoren (ULN2003) abwechselnd rechts/links drehen.

Pinbelegung analog zu src/main.cpp
"""

from machine import Pin
import time

STEPS_PER_REVOLUTION = 2048

ROTATE_RPM = 12
ROTATE_PAUSE_MS = 600

# Halbschritt-Sequenz fuer 28BYJ-48 (IN1, IN2, IN3, IN4)
HALFSTEP_SEQ = (
    (1, 0, 0, 0),
    (1, 1, 0, 0),
    (0, 1, 0, 0),
    (0, 1, 1, 0),
    (0, 0, 1, 0),
    (0, 0, 1, 1),
    (0, 0, 0, 1),
    (1, 0, 0, 1),
)


class Stepper:
    def __init__(self, phase1, phase2, phase3, phase4, steps_per_revolution=STEPS_PER_REVOLUTION):
        self._pins = (
            Pin(phase1, Pin.OUT),
            Pin(phase2, Pin.OUT),
            Pin(phase3, Pin.OUT),
            Pin(phase4, Pin.OUT),
        )
        self._steps_per_revolution = steps_per_revolution
        self._seq_index = 0
        self._step_delay_ms = 2
        self._release()

    def set_speed(self, rpm):
        steps_per_minute = self._steps_per_revolution * rpm
        self._step_delay_ms = max(1, 60000 // steps_per_minute)

    def _write_step(self, seq_index):
        values = HALFSTEP_SEQ[seq_index % len(HALFSTEP_SEQ)]
        for pin, value in zip(self._pins, values):
            pin.value(value)

    def _release(self):
        for pin in self._pins:
            pin.value(0)

    def step(self, steps):
        direction = 1 if steps >= 0 else -1
        for _ in range(abs(steps)):
            self._seq_index = (self._seq_index + direction) % len(HALFSTEP_SEQ)
            self._write_step(self._seq_index)
            time.sleep_ms(self._step_delay_ms)
        self._release()


# Stepper 1
stepper1 = Stepper(21, 47, 34, 35)

# Stepper 2
stepper2 = Stepper(36, 48, 33, 26)

stepper1.set_speed(ROTATE_RPM)
stepper2.set_speed(ROTATE_RPM)

print("ESP32 MicroPython gestartet")

while True:
    print("Stepper 1 rechts")
    stepper1.step(STEPS_PER_REVOLUTION)
    time.sleep_ms(ROTATE_PAUSE_MS)

    print("Stepper 1 links")
    stepper1.step(-STEPS_PER_REVOLUTION)
    time.sleep_ms(ROTATE_PAUSE_MS)

    print("Stepper 2 rechts")
    stepper2.step(STEPS_PER_REVOLUTION)
    time.sleep_ms(ROTATE_PAUSE_MS)

    print("Stepper 2 links")
    stepper2.step(-STEPS_PER_REVOLUTION)
    time.sleep_ms(ROTATE_PAUSE_MS)
