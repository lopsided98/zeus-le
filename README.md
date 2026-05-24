# zeus-le

### Synchronized wireless audio recording system

zeus-le is a system to allow a set of wireless digital audio recorders to maintain better than single sample accurate time synchronization over indefinite periods. A central node broadcasts periodic synchronization packets over Bluetooth LE, which are received by an arbitrary number of recording nodes. The recording nodes use a digital phase-locked loop (PLL) to keep the audio sample clock in sync with the reference. The PLL is implemented using a Kalman filter and linear quadratic regulator.

This repository contains both firmware source code and a custom PCB design for the audio recording hardware. The firmware is written in C for a Nordic nRF53 running Zephyr RTOS. The PCB is designed in KiCad and hosts an nRF53+nRF7002 module, battery charger, SD card, audio ADC and voltage regulators.