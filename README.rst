PIM670 Zabbix Display
=====================

This project aims to show the current *Zabbix* state on the *Pimoroni
Cosmic Unicorn*.

*[... picture goes here ...]*

The *Cosmic Unicorn* has a 32x32 LED matrix and is driven by a
*RaspberryPi Pico W* with integrated Wi-Fi.

The project uses `Pete Favelle's PicoW C/C++ Boilerplate Project
<https://github.com/ahnlak-rp2040/picow-boilerplate>`_ as a starting
point. See `boilerplate docs
<https://ahnlak-rp2040.github.io/picow-boilerplate/>`_ for technical docs.


-------------------
Building/installing
-------------------

Quick build instructions::

    apt install cmake gcc-arm-none-eabi

Jump to a nice library dir and to these::

    git clone --recursive https://github.com/raspberrypi/pico-sdk.git
    git clone --recursive https://github.com/pimoroni/pimoroni-pico.git

Jump to the project dir and do this::

    git clone https://github.com/ossobv/pim670-zabbix-display.git
    cd pim670-zabbix-display
    mkdir -p build && cd build
    cmake -DPICO_SDK_PATH=${libdir:-../../lib}/pico-sdk \
      -DPIMORONI_PICO_PATH=${libdir:-../../lib}/pimoroni-pico ..
    make -j12

This should produce a ``pim670-zabbix-display.uf2`` file.

To install, you press ``BOOTSEL`` and ``RESET`` on the *Cosmic Unicorn*.
This puts the device into flash mode and allows you to (auto)mount the
USB-drive and copy ``pim670-zabbix-display.uf2`` there::

    cp pim670-zabbix-display.uf2 /media/$(whoami)/RPI-RP2/

This restarts the device, and you're good to go.


-------
License
-------

This project released under the BSD 3-Clause License, to match the one
used in the PicoW C/C++ Boilerplate Project by Pete Favelle (ahnlak).

(Some example code might be borrowed from the Pimoroni Pico Project,
which has a BSD-compatible MIT license.)
