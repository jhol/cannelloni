# Cannelloni

Cannelloni is a program based on fxload.c from the libusb1.0 examples, and it aims to be a replacement of fx2pipe, which doesn't function anymore on today's kernels (March 2023) even with the libusb-0.1 compatibility layer.

fx2pipe is GPL 2.0 licensed by Wolfgang Wieser. For more info please see:
http://www.triplespark.net/elec/periph/USB-FX2/software/fx2pipe.html

Cannelloni either reads from stdin and writes to a Cypress FX chip through USB2.0 (outputting to parallel slave 16 bits bus, MSB first), or does the contrary: read from the chip and write to stdout.

It uploads the firmware to chip RAM at start. It uses the same firmware contained in fx2pipe, with the addition of 1 byte of configuration to allow changing the polarity of some flag output pins (they are described in the "arguments migration guide" below). It is compatible with the original firmware.

## Performance

I get 36 MiB/s in/out with my 10-year old Intel i5 PC, using one of those FX2LP chinese modules.

## Installation

Depedencies:

  * libusb-1.0
  * sdcc

To build Cannelloni:

```
./configure
make
```

Finally, to allow access to the USB device, install the needed udev rule and add your user to group ```plugdev```. See ```udev_rule/z70_usbfx2.rules```

## How to run

To run Cannelloni with the provided firmware:

```
./cannelloni -f firmware/fx2pipe.ihx <more options defining your interface>
```

If you have only one device connected it will be detected and configured right away. There are more options to select the device by bus location or vid:pid.

Note that if you use Cannelloni (for output) from your own program, and you want to close it by sending it a signal, perhaps you will need to send another (dummy) block of bytes to let it wake from the I/O call and realize it has to finish (the dummy packet will not be sent to USB as quit has been requested). For input this doesn't matter.

## Program arguments
```
Usage: cannelloni -f <path> [more options]
  -f <path>       -- Firmware to upload (Supported files: .hex, .ihx, .iic, .bix and .img)
  -g <path>       -- Second stage loader firmware file (same supported extensions as -f)
  -t <type>       -- Target type: an21, fx, fx2, fx2lp, fx3. Note: fx3 is not implemented.
  -d <vid:pid>    -- Target device, as an USB VID:PID
  -p <bus,addr>   -- Target device, as a libusb bus number and device address path.
  Note: if -t, -d nor -p are specified, the first supported found device will be used.
  -i              -- Run in IN direction, i.e. USB->Host (read data from USB EP6)(default)
  -o              -- Run in OUT direction, i.e. Host->USB (write data to USB EP2)
  -0              -- No stdin/stdout, discard incoming data/send zeros (For testing speed)
  -w              -- Use 16bit (default) wide fifo bus on FX2 side.
  -8              -- Use 8bit wide fifo bus on FX2 side.
  -4              -- Use quadruple buffered (default) FX2 fifo.
  -3              -- Use triple buffered FX2 fifo.
  -2              -- Use double buffered FX2 fifo.
  -s              -- Run in sync slave fifo mode (default)
  -a              -- Run in async slave fifo mode.
  -b N            -- Set IO buffer size to N bytes (default 16384), even from 2 to 2^31 -2.
  -n M            -- Stop after M bytes, M being a number from 2 to 2^64 - 1.
  Note: M, if specified, must be divisible by N to avoid potential buffer overflow errors.
  -c [x|30[o]|48[o]][i] -- Specify interface clock:
                        x -> External from IFCLK pin.
                        30 or 48 -> Internal clock 30/48MHz (default: 48).
                        Suffix 'o' to enable output to IFCLK pin.
                        'i' to invert IFCLK.
  -z [12|24|48][o|z][i] -- Specify 8051 frequency in MHz (default: 48) and CLKOUT pin:
                        o -> Enable CLKOUT pin driver output.
                        z -> Disable (tristate) CLKOUT pin driver output (default)
                        i -> Invert clock signal on CLKOUT pin.
  -l              -- Invert polarity of 'queue full' flag output pin (i.e., assert high)
  -e              -- Invert polarity of 'queue empty' flag output pin (i.e., assert high)
  -x              -- Invert polarity of 'SLWR' input pin (i.e., assert high)
  -r              -- Invert polarity of 'SLRD' input pin (i.e., assert high)
  -j              -- Invert polarity of 'SLOE' input pin (i.e., assert high)
  -k              -- Invert polarity of 'PKTEND' input flag pin (i.e., assert high)
  -v              -- Increase verbosity.
  -q              -- Decrease verbosity (silent mode)
  -V              -- Print program version.
  -h              -- Print this help.
```

## Migration of program arguments from fx2pipe to Cannelloni

Most program arguments are different from fx2pipe, though some are the same.

- ```--help``` parameter has been renamed to ```-h```
- ```--version``` parameter has been renamed to ```-V```
- ```-i``` parameter remains unchanged.
- ```-o``` parameter remains unchanged.
- ```-0``` parameter remains unchanged.
- ```-8``` parameter remains unchanged.
- ```-w``` parameter remains unchanged.
- ```-2``` parameter remains unchanged.
- ```-3``` parameter remains unchanged.
- ```-4``` parameter remains unchanged.
- ```-O``` parameter is dropped (use ```-o -0```).
- ```-I``` parameter is dropped (use ```-i -0```).
- ```-bs=NNN``` parameter is renamed to ```-b N```, where N (default 16384) is an even number from 2 to 2^31 - 2 bytes.
- ```-ps=NN``` parameter is dropped (there is no substitute).
- ```-sched=P[,N]``` parameter is dropped (there is no substitute).
- ```-fw=PATH``` is now ```-f PATH```, supporting now .hex, .iic, .bix and .img file types in addition to the previously supported .ihx file type.
- New second firmware option ```-g PATH``` to make a two-stage firmware upload (first is loaded -g, then -f firmwares)
- ```-n=NNN``` parameter is renamed to ```-n N```, with N being a number from 0 to 2^64 - 1 bytes (k,M,G suffixes are dropped)
- ```-d=NN``` parameter is dropped (use one of the three following).
- ```-d=VID:PID[:N]``` parameter now is just ```-d VID:PID```, without the ```[:N]``` Nth device specifier, and ```=``` is replaced by a space. The first matching device is used.
- New device specifier: ```-t <type>``` Uses the first FX device of that type (an21, fx, fx2, fx2lp, fx3)
- New device specifier: ```-p <bus,addr>``` Uses the device identified with a libusb bus number and device address path.
- ```-ifclk=[x|30[o]|48[o]][i]``` parameter has been renamed to ```-c [x|30[o]|48[o]][i]``` Additionally, the flag ```o``` is no longer enabled by default.
- ```-cko=[12|24|48][o|z][i]``` parameter has been renamed to ```-z [12|24|48][o|z][i]```. Additionally, the flag ```o``` is no longer enabled by default.
- New option ```-l```: Invert polarity of 'queue full' FF flag output pin (i.e. assert high)
- New option ```-e```: Invert polarity of 'queue empty' EF flag output pin (i.e. assert high)
- New option ```-x```: Invert polarity of 'SLWR' input pin (i.e. assert high)
- New option ```-r```: Invert polarity of 'SLRD' input pin (i.e. assert high)
- New option ```-j```: Invert polarity of 'SLOE' input pin (i.e. assert high)
- New option ```-k```: Invert polarity of 'PKTEND' input flag pin (i.e. assert high)
- Flag options (the ones without values) can be shortened together; i.e., ```-o -0 -w``` can be shortened to ```-o0w```

## Not implemented

### FX3

USB3.0 (FX3) Is not implemented, though the program is prepared to upload FX3 firmware. But I don't have such hardware to test.

Implementing the USB3.0 data transfer perhaps would require using the asynchronous API of libusb-1.0. I've used the synchronous one, which simplifies things a lot.

## License

License is GPL 2.0

Cannelloni - Copyright © 2023 Juan Jose Luna Espinosa (https://github.com/yomboprime/cannelloni)

## Credits

- libusb-1.0 and its examples which this project uses and is based on.
- Wolfgang Wieser - Author of fx2pipe.
