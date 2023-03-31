/*
 * Copyright © 2001 Stephen Williams (steve@icarus.com)
 * Copyright © 2001-2002 David Brownell (dbrownell@users.sourceforge.net)
 * Copyright © 2008 Roger Williams (rawqux@users.sourceforge.net)
 * Copyright © 2012 Pete Batard (pete@akeo.ie)
 * Copyright © 2013 Federico Manzan (f.manzan@gmail.com)
 * Copyright © 2023 Juan Jose Luna Espinosa (https://github.com/yomboprime)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

/**
 *
 * cannelloni
 *
 * Documentation and code: https://github.com/yomboprime/cannelloni
 *
 * A program based on fxload.c from the libusb1.0 examples. It aims to be a
 * replacement of fx2pipe, which doesn't function anymore on today's kernels
 * (March 2023) even with the libusb-0.1 compatibility layer.
 *
 * fx2pipe is GPL 2.0 licensed by Wolfgang Wieser. For more info please see:
 * http://www.triplespark.net/elec/periph/USB-FX2/software/fx2pipe.html
 *
 * Cannelloni either reads from stdin and writes to a Cypress FX USB2.0 chip
 * through USB2.0 (outputting to parallel slave 16 bits bus, MSB first), or does
 * the contrary: read from the chip and write to stdout.
 *
 * It uploads the firmware to chip RAM at start. It uses the same firmware
 * contained in fx2pipe, with the addition of 1 byte of configuration to allow
 * changing the polarity of some flag output pins. It is compatible with the
 * original firmware.
 *
 */

#define CANELLONI_VERSION "cannelloni 0.1.0"

#include "config.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <getopt.h>
#include <time.h>

#include <libusb-1.0/libusb.h>
#include "ezusb.h"

#if !defined(_WIN32) || defined(__CYGWIN__)
#include <syslog.h>
static bool dosyslog = false;
#include <strings.h>
#define _stricmp strcasecmp
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

// The 6 bytes of config which are written to the
// controller RAM after uploading the firmware.
uint8_t firmwareConfig[ 6 ];

// Controller RAM address to write the config to.
const int FirmwareConfigAddr = 0x1003;

void logerror(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	#if !defined(_WIN32) || defined(__CYGWIN__)
	if (dosyslog)
		vsyslog(LOG_ERR, format, ap);
	else
		#endif
		vfprintf(stderr, format, ap);
	va_end(ap);
}

static int print_usage(int error_code) {

	fprintf(stderr, "\n%s", CANELLONI_VERSION);
	fprintf(stderr, " © 2023 by Juan Jose Luna Espinosa. GPL2.0 License.\n");
	fprintf(stderr, "Documentation and code: https://github.com/yomboprime/cannelloni\n");
	fprintf(stderr, "\nUsage: cannelloni -f <path> [more options]\n");
	fprintf(stderr, "  -f <path>       -- Firmware to upload (Supported files: .hex, .ihx, .iic, .bix and .img)\n");
	fprintf(stderr, "  -g <path>       -- Second stage loader firmware file (same supported extensions as -f)\n");
	fprintf(stderr, "  -t <type>       -- Target type: an21, fx, fx2, fx2lp, fx3. Note: fx3 is not implemented.\n");
	fprintf(stderr, "  -d <vid:pid>    -- Target device, as an USB VID:PID\n");
	fprintf(stderr, "  -p <bus,addr>   -- Target device, as a libusb bus number and device address path.\n");
	fprintf(stderr, "  Note: if -t, -d nor -p are specified, the first supported found device will be used.\n");
	fprintf(stderr, "  -i              -- Run in IN direction, i.e. USB->Host (read data from USB EP6)(default)\n");
	fprintf(stderr, "  -o              -- Run in OUT direction, i.e. Host->USB (write data to USB EP2)\n");
	fprintf(stderr, "  -0              -- No stdin/stdout, discard incoming data/send zeros (For testing speed)\n");
	fprintf(stderr, "  -w              -- Use 16bit (default) wide fifo bus on FX2 side.\n");
	fprintf(stderr, "  -8              -- Use 8bit wide fifo bus on FX2 side.\n");
	fprintf(stderr, "  -4              -- Use quadruple buffered (default) FX2 fifo.\n");
	fprintf(stderr, "  -3              -- Use triple buffered FX2 fifo.\n");
	fprintf(stderr, "  -2              -- Use double buffered FX2 fifo.\n");
	fprintf(stderr, "  -s              -- Run in sync slave fifo mode (default)\n");
	fprintf(stderr, "  -a              -- Run in async slave fifo mode.\n");
	fprintf(stderr, "  -b N            -- Set IO buffer size to N bytes (default 16384), even from 2 to 2^31 -2.\n");
	fprintf(stderr, "  -n M            -- Stop after M bytes, M being a number from 2 to 2^64 - 1.\n");
	fprintf(stderr, "  Note: M, if specified, must be divisible by N to avoid potential buffer overflow errors.\n");
	fprintf(stderr, "  -c [x|30[o]|48[o]][i] -- Specify interface clock:\n");
	fprintf(stderr, "                        x -> External from IFCLK pin.\n");
	fprintf(stderr, "                        30 or 48 -> Internal clock 30/48MHz (default: 48).\n");
	fprintf(stderr, "                        Suffix 'o' to enable redirection of clock to CLKOUT pin.\n");
	fprintf(stderr, "                        'i' to invert IFCLK.\n");
	fprintf(stderr, "  -z [12|24|48][o|z][i] -- Specify 8051 frequency in MHz (default: 48) and CLKOUT pin:\n");
	fprintf(stderr, "                        o -> Enable CLKOUT pin driver output.\n");
	fprintf(stderr, "                        z -> Disable (tristate) CLKOUT pin driver output (default)\n");
	fprintf(stderr, "                        i -> Invert clock signal on CLKOUT pin.\n");
	fprintf(stderr, "  -l              -- Invert polarity of 'queue full' flag output pin (i.e., assert high)\n");
	fprintf(stderr, "  -e              -- Invert polarity of 'queue empty' flag output pin (i.e., assert high)\n");
	fprintf(stderr, "  -x              -- Invert polarity of 'SLWR' input pin (i.e., assert high)\n");
	fprintf(stderr, "  -r              -- Invert polarity of 'SLRD' input pin (i.e., assert high)\n");
	fprintf(stderr, "  -j              -- Invert polarity of 'SLOE' input pin (i.e., assert high)\n");
	fprintf(stderr, "  -k              -- Invert polarity of 'PKTEND' input flag pin (i.e., assert high)\n");
	fprintf(stderr, "  -v              -- Increase verbosity.\n");
	fprintf(stderr, "  -q              -- Decrease verbosity (silent mode)\n");
	fprintf(stderr, "  -V              -- Print program version.\n");
	fprintf(stderr, "  -h              -- Print this help.\n");
	return error_code;
}

char doTerminate = 0;
int numSignals = 0;
static void signalHandler(int)
{
	if ( ++numSignals >= 5 ) {
		fprintf(stderr, "\nReceived too many signals, forcibly stopping...\n");
		exit(-1);
	}
	else {
		if (verbose) fprintf(stderr, "\nSignal received, stopping after the current transfer...\n");
		doTerminate = 1;
	}
}

double getTime() {
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	return ( (double) t.tv_sec ) + ( ( double ) t.tv_nsec ) * 0.000000001;
}

char parseOptionC(char *value, char *useIFCLK, char *use48MhzInternalCLK, char *redirectToCLKOUT, char *invertIFCLK)
{
	int pos = 0;
	if (value[pos] == 'x') {
		*useIFCLK = 1;
		pos++;
	} else if (value[pos] == '3' && value[pos + 1] == '0') {
		*use48MhzInternalCLK = 0;
		pos+= 2;
		if (value[pos] == 'o') {
			*redirectToCLKOUT = 1;
			pos++;
		}
	} else if (value[pos] == '4' && value[pos + 1] == '8') {
		*use48MhzInternalCLK = 1;
		pos+= 2;
		if (value[pos] == 'o') {
			*redirectToCLKOUT = 1;
			pos++;
		}
	}

	if (value[pos] == 'i') {
		*invertIFCLK = 1;
		pos++;
	}

	if (value[pos] != 0) {
		return 1;
	}

	return 0;
}

#define MHZ12 0
#define MHZ24 1
#define MHZ48 2
char parseOptionZ(char *value, int *cpuMHz, char *enableCLKOUTDriver, char *invertCLKOUT)
{
	int pos = 0;

	if (value[pos] == '1' && value[pos + 1] == '2') {
		*cpuMHz = MHZ12;
		pos += 2;
	} else if (value[pos] == '2' && value[pos + 1] == '4') {
		*cpuMHz = MHZ24;
		pos += 2;
	} else if (value[pos] == '4' && value[pos + 1] == '8') {
		*cpuMHz = MHZ48;
		pos += 2;
	}

	if (value[pos] == 'o') {
		*enableCLKOUTDriver = 1;
		pos++;
	} else if (value[pos] == 'z') {
		*enableCLKOUTDriver = 0;
		pos++;
	}

	if (value[pos] == 'i') {
		*invertCLKOUT = 1;
		pos++;
	}

	if (value[pos] != 0) {
		return 1;
	}

	return 0;
}

void preResetCallback(libusb_device_handle *device)
{
	if (verbose) logerror("Firmware configuration: %d, %d, %d, %d, %d, %d\n",
		firmwareConfig[ 0 ],
		firmwareConfig[ 1 ],
		firmwareConfig[ 2 ],
		firmwareConfig[ 3 ],
		firmwareConfig[ 4 ],
		firmwareConfig[ 5 ]
	);
	int result = ezusb_write(device, "Write config", 0xA0, FirmwareConfigAddr, firmwareConfig, ARRAYSIZE(firmwareConfig));
	if (result<0 && verbose) {
		logerror("Error writing config in controller.\n");
	}
}

#define FIRMWARE 0
#define LOADER 1
int main(int argc, char*argv[])
{
	fx_known_device known_device[] = FX_KNOWN_DEVICES;
	const char *path[] = { NULL, NULL };
	const char *device_id = NULL;
	const char *device_path = getenv("DEVICE");
	const char *type = NULL;
	const char *fx_name[FX_TYPE_MAX] = FX_TYPE_NAMES;
	const char *ext, *img_name[] = IMG_TYPE_NAMES;
	int fx_type = FX_TYPE_UNDEFINED, img_type[ARRAYSIZE(path)];

	int opt, status;
	unsigned int i, j;
	unsigned vid = 0, pid = 0;
	unsigned busnum = 0, devaddr = 0, _busnum, _devaddr;
	libusb_device *dev, **devs;
	libusb_device_handle *device = NULL;
	struct libusb_device_descriptor desc;

	int alternateFunction;
	char directionIN = 1;
	char disableInOut = 0;
	char use8BitBus = 0;
	int numBuffers = 4;
	char runAsyncBus = 0;

	int blockSize = 16384;
	unsigned int bSize = 0;
	unsigned char *transferBuffer;
	char limitTransfer = 0;
	uint64_t numBytesLimit = 0;
	uint64_t numBytesToTransfer = 0;
	uint64_t totalBytesLeftToTransfer = 0;
	uint64_t totalBytesTransferred = 0;
	int numBytesTransferred;

	char useIFCLK = 0;
	char use48MhzInternalCLK = 1;
	char redirectToCLKOUT = 0;
	char invertIFCLK = 0;

	int cpuMHz = MHZ48;
	char enableCLKOUTDriver = 0;
	char invertCLKOUT = 0;

	char invertQueueFullPin = 0;
	char invertQueueEmptyPin = 0;
	char invertQueueSLWRPin = 0;
	char invertQueueSLRDPin = 0;
	char invertQueueSLOEPin = 0;
	char invertQueuePKTENDPin = 0;

	unsigned char endpoint;

	double time0, time1, deltaTime, speed;

	// Install signal handlers
	signal( SIGTERM, signalHandler );
	signal( SIGINT, signalHandler );

	// Parse arguments

	while ((opt = getopt(argc, argv, "qvV?hiow80432aseljkrxb:zd:p:f:g:t:n:c:z:")) != EOF)

		switch (opt) {

			case 'd':
				device_id = optarg;
				if (sscanf(device_id, "%x:%x" , &vid, &pid) != 2 ) {
					fputs ("Please specify VID & PID as \"vid:pid\" in hexadecimal format.\n", stderr);
					return -1;
				}
				break;

			case 'p':
				device_path = optarg;
				if (sscanf(device_path, "%u,%u", &busnum, &devaddr) != 2 ) {
					fputs ("Please specify bus number & device number as \"bus,dev\" in decimal format.\n", stderr);
					return -1;
				}
				break;

			case 'f':
				path[FIRMWARE] = optarg;
				break;

			case 'g':
				path[LOADER] = optarg;
				break;

			case 't':
				type = optarg;
				break;

			case 'i':
				directionIN = 1;
				break;

			case 'o':
				directionIN = 0;
				break;

			case '0':
				disableInOut = 1;
				break;

			case 'w':
				use8BitBus = 0;
				break;

			case '8':
				use8BitBus = 1;
				break;

			case '4':
				numBuffers = 4;
				break;

			case '3':
				numBuffers = 3;
				break;

			case '2':
				numBuffers = 2;
				break;

			case 'a':
				runAsyncBus = 1;
				break;

			case 's':
				runAsyncBus = 0;
				break;

			case 'b':
				if (sscanf(optarg, "%u" , &bSize) != 1 || bSize < 2 || bSize & 1) {
					fputs ("-b: Please specify a positive, even buffer size in bytes in decimal format.", stderr);
					return -1;
				}
				blockSize = bSize;
				break;

			case 'n':
				limitTransfer = 1;
				if (sscanf(optarg, "%"PRIu64, &numBytesLimit) != 1 || numBytesLimit < 2 || numBytesLimit & 1) {
					fputs ("-n: Please specify a positive, even number of bytes in decimal format.", stderr);
					return -1;
				}
				break;

			case 'c':
				if (parseOptionC( optarg, &useIFCLK, &use48MhzInternalCLK, &redirectToCLKOUT, &invertIFCLK)) {
					return print_usage(-1);
				}
				break;

			case 'z':
				if (parseOptionZ( optarg, &cpuMHz, &enableCLKOUTDriver, &invertCLKOUT)) {
					return print_usage(-1);
				}
				break;

			case 'e':
				invertQueueEmptyPin = 1;
				break;

			case 'l':
				invertQueueFullPin = 1;
				break;

			case 'x':
				invertQueueSLWRPin = 1;
				break;

			case 'r':
				invertQueueSLRDPin = 1;
				break;

			case 'j':
				invertQueueSLOEPin = 1;
				break;

			case 'k':
				invertQueuePKTENDPin = 1;
				break;

			case 'V':
				puts(CANELLONI_VERSION);
				return 0;

			case 'v':
				verbose++;
				break;

			case 'q':
				verbose--;
				break;

			case '?':
			case 'h':
			default:
				return print_usage(-1);

		}

	if (path[FIRMWARE] == NULL) {
		logerror("No firmware specified!\n");
		return print_usage(-1);
	}
	if ((device_id != NULL) && (device_path != NULL)) {
		logerror("Only one of -d or -p can be specified.\n");
		return print_usage(-1);
	}

	if ( numBytesLimit % blockSize != 0 ) {
		logerror("Number of bytes to transfer must be divisible by buffer size.\n");
		return print_usage(-1);
	}

	// Determine the target type
	if (type != NULL) {
		for (i=0; i<FX_TYPE_MAX; i++) {
			if (strcmp(type, fx_name[i]) == 0) {
				fx_type = i;
				break;
			}
		}
		if (i >= FX_TYPE_MAX) {
			logerror("Illegal microcontroller type: %s\n", type);
			return print_usage(-1);
		}
	}

	// Determine the configuration (6 bytes) for the microcontroller RAM
	for ( i = 0; i < ARRAYSIZE( firmwareConfig ); i ++ ) firmwareConfig[ i ] = 0;

	// Byte 0
	firmwareConfig[ 0 ] = directionIN ? 0x12 : 0x21;

	// Byte 1
	if (!useIFCLK) firmwareConfig[ 1 ] |= 1 << 7;
	if (use48MhzInternalCLK) firmwareConfig[ 1 ] |= 1 << 6;
	if (redirectToCLKOUT) firmwareConfig[ 1 ] |= 1 << 5;
	if (invertIFCLK) firmwareConfig[ 1 ] |= 1 << 4;
	if (runAsyncBus) firmwareConfig[ 1 ] |= 1 << 3;
	// Slave FIFO
	firmwareConfig[ 1 ] |= 0x03;

	// Byte 2
	firmwareConfig[ 2 ] = 1 << 7;
	if (directionIN) firmwareConfig[ 2 ] |= 1 << 6;
	// Bulk, 512 bytes
	firmwareConfig[ 2 ] |= 0x20;
	switch (numBuffers) {
		case 2:
			firmwareConfig[ 2 ] |= 0x02;
			break;
		case 3:
			firmwareConfig[ 2 ] |= 0x03;
			break;
		case 4:
		default:
			// Nothing to do: firmwareConfig[ 2 ] |= 0x00;
			break;
	}

	// Byte 3
	firmwareConfig[ 3 ] = directionIN ? 0x0d : 0x11;
	if ( use8BitBus ) firmwareConfig[ 3 ] &= 0xFE;

	// Byte 4
	switch (cpuMHz) {
		case MHZ12:
			// Nothing to do: firmwareConfig[ 4 ] |= 0x00;
			break;
		case MHZ24:
			firmwareConfig[ 4 ] |= 0x08;
			break;
		case MHZ48:
		default:
			firmwareConfig[ 4 ] |= 0x10;
			break;
	}
	if (invertCLKOUT) firmwareConfig[ 4 ] |= 1 << 2;
	if (enableCLKOUTDriver) firmwareConfig[ 4 ] |= 1 << 1;

	// Byte 5
	if (invertQueueFullPin) firmwareConfig[ 5 ] |= 1 << 0;
	if (invertQueueEmptyPin) firmwareConfig[ 5 ] |= 1 << 1;
	if (invertQueueSLWRPin) firmwareConfig[ 5 ] |= 1 << 2;
	if (invertQueueSLRDPin) firmwareConfig[ 5 ] |= 1 << 3;
	if (invertQueueSLOEPin) firmwareConfig[ 5 ] |= 1 << 4;
	if (invertQueuePKTENDPin) firmwareConfig[ 5 ] |= 1 << 5;


	if ( doTerminate ) exit(-1);

	// -- Finished configuration preparation. Start accessing the device with libusb

	// Init libusb
	status = libusb_init(NULL);
	if (status < 0) {
		logerror("libusb_init() failed: %s\n", libusb_error_name(status));
		return -1;
	}
	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, verbose);

	// Match parameters to known devices, and open the matched device
	if ((type == NULL) || (device_id == NULL) || (device_path != NULL)) {
		if (libusb_get_device_list(NULL, &devs) < 0) {
			logerror("libusb_get_device_list() failed: %s\n", libusb_error_name(status));
			goto err;
		}
		for (i=0; (dev=devs[i]) != NULL; i++) {
			_busnum = libusb_get_bus_number(dev);
			_devaddr = libusb_get_device_address(dev);
			if ((type != NULL) && (device_path != NULL)) {
				// If both a type and bus,addr were specified, we just need to find our match
				if ((libusb_get_bus_number(dev) == busnum) && (libusb_get_device_address(dev) == devaddr))
					break;
			} else {
				status = libusb_get_device_descriptor(dev, &desc);
				if (status >= 0) {
					if (verbose >= 3) {
						logerror("examining %04x:%04x (%d,%d)\n",
									desc.idVendor, desc.idProduct, _busnum, _devaddr);
					}
					for (j=0; j<ARRAYSIZE(known_device); j++) {
						if ((desc.idVendor == known_device[j].vid)
							&& (desc.idProduct == known_device[j].pid)) {
							if (// Nothing was specified
								((type == NULL) && (device_id == NULL) && (device_path == NULL)) ||
								// vid:pid was specified and we have a match
								((type == NULL) && (device_id != NULL) && (vid == desc.idVendor) && (pid == desc.idProduct)) ||
								// bus,addr was specified and we have a match
								((type == NULL) && (device_path != NULL) && (busnum == _busnum) && (devaddr == _devaddr)) ||
								// type was specified and we have a match
								((type != NULL) && (device_id == NULL) && (device_path == NULL) && (fx_type == known_device[j].type)) ) {
								fx_type = known_device[j].type;
							vid = desc.idVendor;
							pid = desc.idProduct;
							busnum = _busnum;
							devaddr = _devaddr;
							break;
								}
							}
					}
					if (j < ARRAYSIZE(known_device)) {
						if (verbose)
							logerror("found device '%s' [%04x:%04x] (%d,%d)\n",
										known_device[j].designation, vid, pid, busnum, devaddr);
							break;
					}
				}
			}
		}
		if (dev == NULL) {
			libusb_free_device_list(devs, 1);
			libusb_exit(NULL);
			logerror("Could not find a known device - please specify type and/or vid:pid and/or bus,dev\n");
			return print_usage(-1);
		}
		status = libusb_open(dev, &device);
		libusb_free_device_list(devs, 1);
		if (status < 0) {
			logerror("libusb_open() failed: %s\n", libusb_error_name(status));
			goto err;
		}
	} else if (device_id != NULL) {
		device = libusb_open_device_with_vid_pid(NULL, (uint16_t)vid, (uint16_t)pid);
		if (device == NULL) {
			logerror("libusb_open() failed\n");
			goto err;
		}
	}

	// Set auto-detach of kernel driver
	libusb_set_auto_detach_kernel_driver(device, 1);

	// We need to claim the first interface (index 0) for control messages in order to program the controller
	status = libusb_claim_interface(device, 0);
	if (status != LIBUSB_SUCCESS) {
		libusb_close(device);
		logerror("libusb_claim_interface failed: %s\n", libusb_error_name(status));
		goto err;
	}

	if (verbose)
		logerror("microcontroller type: %s\n", fx_name[fx_type]);

	// Load the firmware image in memory
	for (i=0; i<ARRAYSIZE(path); i++) {
		if (path[i] != NULL) {
			ext = path[i] + strlen(path[i]) - 4;
			if ((_stricmp(ext, ".hex") == 0) || (strcmp(ext, ".ihx") == 0))
				img_type[i] = IMG_TYPE_HEX;
			else if (_stricmp(ext, ".iic") == 0)
				img_type[i] = IMG_TYPE_IIC;
			else if (_stricmp(ext, ".bix") == 0)
				img_type[i] = IMG_TYPE_BIX;
			else if (_stricmp(ext, ".img") == 0)
				img_type[i] = IMG_TYPE_IMG;
			else {
				logerror("%s is not a recognized image type.\n", path[i]);
				libusb_release_interface(device, 0);
				libusb_close(device);
				goto err;
			}
		}
		if (verbose && path[i] != NULL)
			logerror("%s: type %s\n", path[i], img_name[img_type[i]]);
	}

	// Program the firmware in the microcontroller
	if (path[LOADER] == NULL) {
		// Single stage, put into internal memory
		if (verbose > 1)
			logerror("single stage: load on-chip memory\n");
		status = ezusb_load_ram(device, path[FIRMWARE], fx_type, img_type[FIRMWARE], 0, preResetCallback);
	} else {
		// two-stage, put loader into external memory
		if (verbose > 1)
			logerror("1st stage: load 2nd stage loader\n");
		status = ezusb_load_ram(device, path[LOADER], fx_type, img_type[LOADER], 0, preResetCallback);
		if (status == 0) {
			// two-stage, put firmware into internal memory
			if (verbose > 1)
				logerror("2nd state: load on-chip memory\n");
			status = ezusb_load_ram(device, path[FIRMWARE], fx_type, img_type[FIRMWARE], 1, NULL);
		}
	}

	// Release interface and close device
	libusb_release_interface(device, 0);
	libusb_close(device);

	// After device has been programmed, we reopen the device for the data transfer.

	if (dev) {
		status = libusb_open(dev, &device);
		if (status < 0) {
			logerror("libusb_open() failed for data transfer: %s\n", libusb_error_name(status));
			goto err;
		}
	} else {
		device = libusb_open_device_with_vid_pid(NULL, (uint16_t)vid, (uint16_t)pid);
		if (device == NULL) {
			logerror("libusb_open() failed for data transfer.\n");
			goto err;
		}
	}

	// Claim interface 0 again
	status = libusb_claim_interface(device, 0);
	if (status != LIBUSB_SUCCESS) {
		libusb_close(device);
		logerror("libusb_claim_interface failed for data transfer: %s\n", libusb_error_name(status));
		goto err;
	}

	// Set interface 0 alternate function corresponding to transfer type
	// TODO: Currently only transfer type 1 supported (bulk)
	alternateFunction = 1;
	status = libusb_set_interface_alt_setting(device, 0, alternateFunction);
	if (status != LIBUSB_SUCCESS) {
		libusb_release_interface(device, 0);
		libusb_close(device);
		logerror("libusb_set_interface_alt_setting failed for data transfer: %s\n", libusb_error_name(status));
		goto err;
	}

	// Allocate transfer buffer
	transferBuffer = malloc(blockSize);
	if (transferBuffer == NULL) {
		logerror("Not enough system memory to allocate transfer block buffer (%d bytes). Closing.", blockSize);
		doTerminate = 1;
	}

	// Select endpoint
	endpoint = directionIN ? 0x86 : 0x02;

	if (limitTransfer) totalBytesLeftToTransfer = numBytesLimit;

	time0 = getTime();

	// Data transfer loop
	while (!doTerminate) {

		numBytesToTransfer = blockSize;
		if (limitTransfer && numBytesToTransfer >= totalBytesLeftToTransfer ) {
			numBytesToTransfer = totalBytesLeftToTransfer;
			doTerminate = 1;
		}

		if (!directionIN) {
			if (!disableInOut) {
				// Read from stdin
				if (fread(transferBuffer, numBytesToTransfer, 1, stdin) != 1 ) {
					logerror("Error reading data from stdin. Stopping.");
					doTerminate = 1;
					break;
				}
				if (doTerminate) break;
			} else memset(transferBuffer, 0, numBytesToTransfer);
		}

		// Do the actual data transfer
		status = libusb_bulk_transfer(device, endpoint, transferBuffer, numBytesToTransfer, &numBytesTransferred, 1000);
		if (status) {
			logerror("Data transfer failed: %s\n", libusb_error_name(status));
			doTerminate = 1;
			break;
		}

		totalBytesLeftToTransfer -= numBytesTransferred;
		totalBytesTransferred += numBytesTransferred;

		if (directionIN && !disableInOut)
			// Write to stdout
			if (fwrite(transferBuffer, numBytesTransferred, 1, stdout) != 1 ) {
				logerror("Error writing to stdout. Stopping.");
				doTerminate = 1;
				break;
			}

		if (numBytesTransferred < numBytesToTransfer) {
			logerror("Input data received for transfer was less than expected.");
		}

	}

	time1 = getTime();

	// Free transfer buffer
	free(transferBuffer);

	// Release interface
	libusb_release_interface(device, 0);

	// Close device
	libusb_close(device);

	// Exit libusb
	libusb_exit(NULL);

	// Print result statistics

	if (verbose) {

		// Seconds
		deltaTime = time1 - time0;
		// MiB/s
		speed = ( totalBytesTransferred / (1024.0 * 1024.0) ) / deltaTime;

		fprintf(stderr, "Transferred %"PRIu64" bytes in %.2f seconds (%.2f MiB/s)\n", totalBytesTransferred, deltaTime, speed);

	}

	return status;

	err:
	libusb_exit(NULL);
	return -1;
}
