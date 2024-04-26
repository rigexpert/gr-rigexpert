# RigExpert Fobos SDR source block

This is the Fobos SDR receiver Complex IQ signal source block for GnuRadio. Full sourse code.

## Requirements

- Ubuntu 22.04 LTS
- GnuRadio v.3.10

## Dependencies

- libusb-1.0-0-dev 2:1.0.25

## How to build and install

git clone [this repo]<br />
cd gr-rigexpert<br />
sudo cp fobos-sdr.rules /etc/udev/rules.d/00-fobos-sdr.rules<br />
sudo udevadm control --reload-rules<br />
sudo udevadm trigger<br />
mkdir build<br />
cd build<br />
cmake ..<br />
make<br />
sudo make install<br />
sudo ldconfig<br />

## how to update md5 (in build **directory**)

- run md5sum ../include/gnuradio/RigExpert/fobos_sdr.h > sha.txt
- copy md5 value (32 hex chars)
- open ../python/RigExpert/bindings/fobos_sdr_python.cc
- replace pevious /* BINDTOOL_HEADER_FILE_HASH(f06b7121ccf4e00c9fa9af4432643237)                     */

or just

$ gr_modtool bind -u <module_name>

## How to use

Nothing special.
- Place Fobos SDR source on the GRC worksheet
- Connect output node to other nodes
- Run and have a fun

## How it looks like

<img src="./showimg/Screenshot001.png" scale="50%"/><br />
<img src="./showimg/Screenshot002.png" scale="50%"/><br />
<img src="./showimg/Screenshot003.png" scale="50%"/><br />

## What is actually Fobos SDR

For more info see the main product page

https://rigexpert.com/en/products/kits-en/fobos-sdr/
