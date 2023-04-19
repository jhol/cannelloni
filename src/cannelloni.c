/*
 * Copyright © 2001 Stephen Williams (steve@icarus.com)
 * Copyright © 2001-2002 David Brownell (dbrownell@users.sourceforge.net)
 * Copyright © 2008 Roger Williams (rawqux@users.sourceforge.net)
 * Copyright © 2012 Pete Batard (pete@akeo.ie)
 * Copyright © 2013 Federico Manzan (f.manzan@gmail.com)
 * Copyright © 2023 Juan Jose Luna Espinosa (https://github.com/yomboprime)
 * Copyright © 2023 NVIDIA Corporation (jholdsworth@nvidia.com)
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

#include "config.h"
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
uint8_t firmware_config[ 6 ];

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
	fputs(
		"\n"
		PACKAGE_STRING " © 2023 by Juan Jose Luna Espinosa. GPL2.0 License.\n"
		"Documentation and code: " PACKAGE_URL "\n"
		"\n"
		"Usage: cannelloni -f <path> [more options]\n"
		"  -f <path>       -- Firmware to upload (Supported files: .hex, .ihx, .iic, .bix and .img)\n"
		"  -g <path>       -- Second stage loader firmware file (same supported extensions as -f)\n"
		"  -t <type>       -- Target type: an21, fx, fx2, fx2lp, fx3. Note: fx3 is not implemented.\n"
		"  -d <vid:pid>    -- Target device, as an USB VID:PID\n"
		"  -p <bus,addr>   -- Target device, as a libusb bus number and device address path.\n"
		"  Note: if -t, -d nor -p are specified, the first supported found device will be used.\n"
		"  -i              -- Run in IN direction, i.e. USB->Host (read data from USB EP6)(default)\n"
		"  -o              -- Run in OUT direction, i.e. Host->USB (write data to USB EP2)\n"
		"  -0              -- No stdin/stdout, discard incoming data/send zeros (For testing speed)\n"
		"  -w              -- Use 16bit (default) wide fifo bus on FX2 side.\n"
		"  -8              -- Use 8bit wide fifo bus on FX2 side.\n"
		"  -4              -- Use quadruple buffered (default) FX2 fifo.\n"
		"  -3              -- Use triple buffered FX2 fifo.\n"
		"  -2              -- Use double buffered FX2 fifo.\n"
		"  -s              -- Run in sync slave fifo mode (default)\n"
		"  -a              -- Run in async slave fifo mode.\n"
		"  -b N            -- Set IO buffer size to N bytes (default 16384), even from 2 to 2^31 -2.\n"
		"  -n M            -- Stop after M bytes, M being a number from 2 to 2^64 - 1.\n"
		"  Note: M, if specified, must be divisible by N to avoid potential buffer overflow errors.\n"
		"  -c [x|30[o]|48[o]][i] -- Specify interface clock:\n"
		"                        x -> External from IFCLK pin.\n"
		"                        30 or 48 -> Internal clock 30/48MHz (default: 48).\n"
		"                        Suffix 'o' to enable output to IFCLK pin.\n"
		"                        'i' to invert IFCLK.\n"
		"  -z [12|24|48][o|z][i] -- Specify 8051 frequency in MHz (default: 48) and CLKOUT pin:\n"
		"                        o -> Enable CLKOUT pin driver output.\n"
		"                        z -> Disable (tristate) CLKOUT pin driver output (default)\n"
		"                        i -> Invert clock signal on CLKOUT pin.\n"
		"  -l              -- Invert polarity of 'queue full' flag output pin (i.e., assert high)\n"
		"  -e              -- Invert polarity of 'queue empty' flag output pin (i.e., assert high)\n"
		"  -x              -- Invert polarity of 'SLWR' input pin (i.e., assert high)\n"
		"  -r              -- Invert polarity of 'SLRD' input pin (i.e., assert high)\n"
		"  -j              -- Invert polarity of 'SLOE' input pin (i.e., assert high)\n"
		"  -k              -- Invert polarity of 'PKTEND' input flag pin (i.e., assert high)\n"
		"  -v              -- Increase verbosity.\n"
		"  -q              -- Decrease verbosity (silent mode)\n"
		"  -V              -- Print program version.\n"
		"  -h              -- Print this help.\n",
		error_code == 0 ? stdout : stderr);
	return error_code;
}

char do_terminate = 0;
int num_signals = 0;
static void signal_handler(int)
{
	if ( ++num_signals >= 5 ) {
		fprintf(stderr, "\nReceived too many signals, forcibly stopping...\n");
		exit(-1);
	}
	else {
		if (verbose) fprintf(stderr, "\nSignal received, stopping after the current transfer...\n");
		do_terminate = 1;
	}
}

static double get_time() {
	struct timespec t;
	clock_gettime( CLOCK_MONOTONIC, &t );
	return ( (double) t.tv_sec ) + ( ( double ) t.tv_nsec ) * 0.000000001;
}

static char parse_option_c(char *value, char *use_external_ifclk, char *use_48mhz_internal_clk, char *enable_ifclk_output, char *invert_ifclk)
{
	int pos = 0;
	if (value[pos] == 'x') {
		*use_external_ifclk = 1;
		pos++;
	} else if (value[pos] == '3' && value[pos + 1] == '0') {
		*use_48mhz_internal_clk = 0;
		pos+= 2;
		if (value[pos] == 'o') {
			*enable_ifclk_output = 1;
			pos++;
		}
	} else if (value[pos] == '4' && value[pos + 1] == '8') {
		*use_48mhz_internal_clk = 1;
		pos+= 2;
		if (value[pos] == 'o') {
			*enable_ifclk_output = 1;
			pos++;
		}
	}

	if (value[pos] == 'i') {
		*invert_ifclk = 1;
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
static char parse_option_z(char *value, int *cpu_mhz, char *enable_clkout_output, char *invert_clkout)
{
	int pos = 0;

	if (value[pos] == '1' && value[pos + 1] == '2') {
		*cpu_mhz = MHZ12;
		pos += 2;
	} else if (value[pos] == '2' && value[pos + 1] == '4') {
		*cpu_mhz = MHZ24;
		pos += 2;
	} else if (value[pos] == '4' && value[pos + 1] == '8') {
		*cpu_mhz = MHZ48;
		pos += 2;
	}

	if (value[pos] == 'o') {
		*enable_clkout_output = 1;
		pos++;
	} else if (value[pos] == 'z') {
		*enable_clkout_output = 0;
		pos++;
	}

	if (value[pos] == 'i') {
		*invert_clkout = 1;
		pos++;
	}

	if (value[pos] != 0) {
		return 1;
	}

	return 0;
}

static void pre_reset_callback(libusb_device_handle *device)
{
	if (verbose) logerror("Firmware configuration: %d, %d, %d, %d, %d, %d\n",
		firmware_config[ 0 ],
		firmware_config[ 1 ],
		firmware_config[ 2 ],
		firmware_config[ 3 ],
		firmware_config[ 4 ],
		firmware_config[ 5 ]
	);
	int result = ezusb_write(device, "Write config", 0xA0, FirmwareConfigAddr, firmware_config, ARRAYSIZE(firmware_config));
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

	int alternate_function;
	char direction_in = 1;
	char disable_in_out = 0;
	char use_8bit_bus = 0;
	int num_buffers = 4;
	char run_async_bus = 0;

	int block_size = 16384;
	unsigned int bSize = 0;
	unsigned char *transfer_buffer;
	char limit_transfer = 0;
	uint64_t num_bytes_limit = 0;
	uint64_t num_bytes_to_transfer = 0;
	uint64_t total_bytes_left_to_transfer = 0;
	uint64_t total_bytes_transferred = 0;
	int num_bytes_transferred;

	char use_external_ifclk = 0;
	char use_48mhz_internal_clk = 1;
	char enable_ifclk_output = 0;
	char invert_ifclk = 0;

	int cpu_mhz = MHZ48;
	char enable_clkout_output = 0;
	char invert_clkout = 0;

	char invert_queue_full_pin = 0;
	char invert_queue_empty_pin = 0;
	char invert_queue_slwr_pin = 0;
	char invert_queue_slrd_pin = 0;
	char invert_queue_sloe_pin = 0;
	char invert_queue_pktend_pin = 0;

	unsigned char endpoint;

	double time0, time1, delta_time, speed;

	// Install signal handlers
	signal( SIGTERM, signal_handler );
	signal( SIGINT, signal_handler );

	// Parse arguments

	while ((opt = getopt(argc, argv, "qvV?hiow80432aseljkrxuz:b:d:p:f:g:t:n:c:")) != EOF)

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
			direction_in = 1;
			break;

		case 'o':
			direction_in = 0;
			break;

		case '0':
			disable_in_out = 1;
			break;

		case 'w':
			use_8bit_bus = 0;
			break;

		case '8':
			use_8bit_bus = 1;
			break;

		case '4':
			num_buffers = 4;
			break;

		case '3':
			num_buffers = 3;
			break;

		case '2':
			num_buffers = 2;
			break;

		case 'a':
			run_async_bus = 1;
			break;

		case 's':
			run_async_bus = 0;
			break;

		case 'b':
			if (sscanf(optarg, "%u" , &bSize) != 1 || bSize < 2 || bSize & 1) {
				fputs ("-b: Please specify a positive, even buffer size in bytes in decimal format.", stderr);
				return -1;
			}
			block_size = bSize;
			break;

		case 'n':
			limit_transfer = 1;
			if (sscanf(optarg, "%"PRIu64, &num_bytes_limit) != 1 || num_bytes_limit < 2 || num_bytes_limit & 1) {
				fputs ("-n: Please specify a positive, even number of bytes in decimal format.", stderr);
				return -1;
			}
			break;

		case 'c':
			if (parse_option_c( optarg, &use_external_ifclk, &use_48mhz_internal_clk, &enable_ifclk_output, &invert_ifclk)) {
				return print_usage(-1);
			}
			break;

		case 'z':
			if (parse_option_z( optarg, &cpu_mhz, &enable_clkout_output, &invert_clkout)) {
				return print_usage(-1);
			}
			break;

		case 'e':
			invert_queue_empty_pin = 1;
			break;

		case 'l':
			invert_queue_full_pin = 1;
			break;

		case 'x':
			invert_queue_slwr_pin = 1;
			break;

		case 'r':
			invert_queue_slrd_pin = 1;
			break;

		case 'j':
			invert_queue_sloe_pin = 1;
			break;

		case 'k':
			invert_queue_pktend_pin = 1;
			break;

		case 'V':
			puts(PACKAGE_STRING);
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
			return print_usage(0);
		}

	if (!path[FIRMWARE]) {
		logerror("No firmware specified!\n");
		return print_usage(-1);
	}
	if (device_id && device_path) {
		logerror("Only one of -d or -p can be specified.\n");
		return print_usage(-1);
	}

	if ( num_bytes_limit % block_size != 0 ) {
		logerror("Number of bytes to transfer must be divisible by buffer size.\n");
		return print_usage(-1);
	}

	// Determine the target type
	if (type) {
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
	memset(firmware_config, 0, sizeof(firmware_config));

	// Byte 0
	firmware_config[ 0 ] = direction_in ? 0x12 : 0x21;

	// Byte 1
	if (!use_external_ifclk) firmware_config[ 1 ] |= 1 << 7;
	if (use_48mhz_internal_clk) firmware_config[ 1 ] |= 1 << 6;
	if (enable_ifclk_output) firmware_config[ 1 ] |= 1 << 5;
	if (invert_ifclk) firmware_config[ 1 ] |= 1 << 4;
	if (run_async_bus) firmware_config[ 1 ] |= 1 << 3;
	// Slave FIFO
	firmware_config[ 1 ] |= 0x03;

	// Byte 2
	firmware_config[ 2 ] = 1 << 7;
	if (direction_in) firmware_config[ 2 ] |= 1 << 6;
	// Bulk, 512 bytes
	firmware_config[ 2 ] |= 0x20;
	switch (num_buffers) {
	case 2:
		firmware_config[ 2 ] |= 0x02;
		break;
	case 3:
		firmware_config[ 2 ] |= 0x03;
		break;
	case 4:
	default:
		// Nothing to do: firmware_config[ 2 ] |= 0x00;
		break;
	}

	// Byte 3
	firmware_config[ 3 ] = direction_in ? 0x0d : 0x11;
	if ( use_8bit_bus ) firmware_config[ 3 ] &= 0xFE;

	// Byte 4
	switch (cpu_mhz) {
	case MHZ12:
		// Nothing to do: firmware_config[ 4 ] |= 0x00;
		break;
	case MHZ24:
		firmware_config[ 4 ] |= 0x08;
		break;
	case MHZ48:
	default:
		firmware_config[ 4 ] |= 0x10;
		break;
	}
	if (invert_clkout) firmware_config[ 4 ] |= 1 << 2;
	if (enable_clkout_output) firmware_config[ 4 ] |= 1 << 1;

	// Byte 5
	if (invert_queue_full_pin) firmware_config[ 5 ] |= 1 << 0;
	if (invert_queue_empty_pin) firmware_config[ 5 ] |= 1 << 1;
	if (invert_queue_slwr_pin) firmware_config[ 5 ] |= 1 << 2;
	if (invert_queue_slrd_pin) firmware_config[ 5 ] |= 1 << 3;
	if (invert_queue_sloe_pin) firmware_config[ 5 ] |= 1 << 4;
	if (invert_queue_pktend_pin) firmware_config[ 5 ] |= 1 << 5;


	if ( do_terminate ) exit(-1);

	// -- Finished configuration preparation. Start accessing the device with libusb

	// Init libusb
	status = libusb_init(NULL);
	if (status < 0) {
		logerror("libusb_init() failed: %s\n", libusb_error_name(status));
		return -1;
	}
	libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, verbose);

	// Match parameters to known devices, and open the matched device
	if (!type || !device_id || device_path) {
		if (libusb_get_device_list(NULL, &devs) < 0) {
			logerror("libusb_get_device_list() failed: %s\n", libusb_error_name(status));
			goto err;
		}
		for (i=0; (dev=devs[i]) != NULL; i++) {
			_busnum = libusb_get_bus_number(dev);
			_devaddr = libusb_get_device_address(dev);
			if (type && device_path) {
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
								(!type && !device_id && !device_path) ||
								// vid:pid was specified and we have a match
								(!type && device_id && (vid == desc.idVendor) && (pid == desc.idProduct)) ||
								// bus,addr was specified and we have a match
								(!type && device_path && (busnum == _busnum) && (devaddr == _devaddr)) ||
								// type was specified and we have a match
								(type && !device_id && !device_path && (fx_type == known_device[j].type)) ) {
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
		if (!dev) {
			libusb_free_device_list(devs, 1);
			libusb_exit(NULL);
			logerror("Could not find a known device - please make sure it is connected. Or specify type, vid:pid and/or bus,dev\n");
			return -1;
		}
		status = libusb_open(dev, &device);
		libusb_free_device_list(devs, 1);
		if (status < 0) {
			logerror("libusb_open() failed: %s\n", libusb_error_name(status));
			goto err;
		}
	} else if (device_id) {
		device = libusb_open_device_with_vid_pid(NULL, (uint16_t)vid, (uint16_t)pid);
		if (!device) {
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
		if (path[i]) {
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
	if (!path[LOADER]) {
		// Single stage, put into internal memory
		if (verbose > 1)
			logerror("single stage: load on-chip memory\n");
		status = ezusb_load_ram(device, path[FIRMWARE], fx_type, img_type[FIRMWARE], 0, pre_reset_callback);
	} else {
		// two-stage, put loader into external memory
		if (verbose > 1)
			logerror("1st stage: load 2nd stage loader\n");
		status = ezusb_load_ram(device, path[LOADER], fx_type, img_type[LOADER], 0, pre_reset_callback);
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

	// Exit if loading the firmware or programming the device went wrong
	if (status < 0) goto err;

	// After device has been programmed, we reopen the device for the data transfer.

	if (dev) {
		status = libusb_open(dev, &device);
		if (status < 0) {
			logerror("libusb_open() failed for data transfer: %s\n", libusb_error_name(status));
			goto err;
		}
	} else {
		device = libusb_open_device_with_vid_pid(NULL, (uint16_t)vid, (uint16_t)pid);
		if (!device) {
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
	alternate_function = 1;
	status = libusb_set_interface_alt_setting(device, 0, alternate_function);
	if (status != LIBUSB_SUCCESS) {
		libusb_release_interface(device, 0);
		libusb_close(device);
		logerror("libusb_set_interface_alt_setting failed for data transfer: %s\n", libusb_error_name(status));
		goto err;
	}

	// Allocate transfer buffer
	transfer_buffer = malloc(block_size);
	if (!transfer_buffer) {
		logerror("Not enough system memory to allocate transfer block buffer (%d bytes). Closing.", block_size);
		do_terminate = 1;
	}

	// Select endpoint
	endpoint = direction_in ? 0x86 : 0x02;

	if (limit_transfer) total_bytes_left_to_transfer = num_bytes_limit;

	time0 = get_time();

	// Data transfer loop
	while (!do_terminate) {

		num_bytes_to_transfer = block_size;
		if (limit_transfer && num_bytes_to_transfer >= total_bytes_left_to_transfer ) {
			num_bytes_to_transfer = total_bytes_left_to_transfer;
			do_terminate = 1;
		}

		if (!direction_in) {
			if (!disable_in_out) {
				// Read from stdin
				if (fread(transfer_buffer, num_bytes_to_transfer, 1, stdin) != 1 ) {
					if (!feof(stdin)) logerror("Error reading data from stdin. Stopping.\n");
					else if (verbose) logerror("stdin has reached EOF. Stopping.\n");
					do_terminate = 1;
					break;
				}
				if (do_terminate) break;
			} else memset(transfer_buffer, 0, num_bytes_to_transfer);
		}

		// Do the actual data transfer
		status = libusb_bulk_transfer(device, endpoint, transfer_buffer, num_bytes_to_transfer, &num_bytes_transferred, 1000);
		if (status) {
			logerror("Data transfer failed: %s\n", libusb_error_name(status));
			do_terminate = 1;
			break;
		}

		total_bytes_left_to_transfer -= num_bytes_transferred;
		total_bytes_transferred += num_bytes_transferred;

		if (direction_in && !disable_in_out)
			// Write to stdout
			if (fwrite(transfer_buffer, num_bytes_transferred, 1, stdout) != 1 ) {
				logerror("Error writing to stdout. Stopping.");
				do_terminate = 1;
				break;
			}

		if (num_bytes_transferred < num_bytes_to_transfer) {
			logerror("Input data received for transfer was less than expected.");
		}

	}

	time1 = get_time();

	// Free transfer buffer
	free(transfer_buffer);

	// Release interface
	libusb_release_interface(device, 0);

	// Close device
	libusb_close(device);

	// Exit libusb
	libusb_exit(NULL);

	// Print result statistics

	if (verbose) {

		// Seconds
		delta_time = time1 - time0;
		// MiB/s
		speed = ( total_bytes_transferred / (1024.0 * 1024.0) ) / delta_time;

		fprintf(stderr, "Transferred %"PRIu64" bytes in %.2f seconds (%.2f MiB/s)\n", total_bytes_transferred, delta_time, speed);

	}

	return status;

	err:
	libusb_exit(NULL);
	return -1;
}
