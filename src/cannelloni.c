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
 * through USB2.0 (outputting to parallel slave 16 bits bus, LSB first), or does
 * the contrary: read from the chip and write to stdout.
 *
 * It uploads the firmware to chip RAM at start. It uses the same firmware
 * contained in fx2pipe, with the addition of 1 byte of configuration to allow
 * changing the polarity of some flag output pins. It is compatible with the
 * original firmware.
 *
 */

#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <getopt.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <unistd.h>

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

#define UNLIMITED		(UINT64_MAX)
#define NUM_TRANSFERS		(32)
#define MAX_EMPTY_TRANSFERS	(8)
#define MAX_POLL_TIMEOUT	(1000)

struct cannelloni_state {
	libusb_device_handle *handle;
	bool abort;
	int num_signals;
	bool direction_in;
	FILE *file;
	size_t block_size;
	uint64_t total_bytes_left_to_transfer;
	uint64_t total_bytes_transferred;
	struct libusb_transfer **transfers;
	int submitted_transfers;
	int empty_transfer_count;
};

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
		"  -b N            -- Set IO buffer size to N bytes (default 16384), even from 2 to 2^31 -1.\n"
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

static uint64_t get_time(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)(t.tv_sec * 1000000000ULL) + t.tv_nsec;
}

static bool parse_option_c(const char *value, bool *use_external_ifclk, bool *use_48mhz_internal_clk,
	bool *enable_ifclk_output, bool *invert_ifclk)
{
	int pos = 0;
	if (value[pos] == 'x') {
		*use_external_ifclk = true;
		pos++;
	} else if (value[pos] == '3' && value[pos + 1] == '0') {
		*use_48mhz_internal_clk = false;
		pos+= 2;
		if (value[pos] == 'o') {
			*enable_ifclk_output = true;
			pos++;
		}
	} else if (value[pos] == '4' && value[pos + 1] == '8') {
		*use_48mhz_internal_clk = true;
		pos+= 2;
		if (value[pos] == 'o') {
			*enable_ifclk_output = true;
			pos++;
		}
	}

	if (value[pos] == 'i') {
		*invert_ifclk = true;
		pos++;
	}

	return value[pos] != 0;
}

#define MHZ12 0
#define MHZ24 1
#define MHZ48 2
static char parse_option_z(const char *value, int *cpu_mhz, bool *enable_clkout_output, bool *invert_clkout)
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
		*enable_clkout_output = true;
		pos++;
	} else if (value[pos] == 'z') {
		*enable_clkout_output = false;
		pos++;
	}

	if (value[pos] == 'i') {
		*invert_clkout = true;
		pos++;
	}

	return value[pos] != 0;
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

static int get_image_type(const char *path)
{
	const char *const ext = path + strlen(path) - 4;
	if ((_stricmp(ext, ".hex") == 0) || (strcmp(ext, ".ihx") == 0))
		return IMG_TYPE_HEX;
	else if (_stricmp(ext, ".iic") == 0)
		return IMG_TYPE_IIC;
	else if (_stricmp(ext, ".bix") == 0)
		return IMG_TYPE_BIX;
	else if (_stricmp(ext, ".img") == 0)
		return IMG_TYPE_IMG;
	else
		return IMG_TYPE_UNDEFINED;
}

static int load_image(libusb_device_handle *device, const char *path, int fx_type, int stage,
	void (*pre_reset_callback)(libusb_device_handle *))
{
	static const char *const img_names[] = IMG_TYPE_NAMES;

	const int img_type = get_image_type(path);
	if (img_type == IMG_TYPE_UNDEFINED) {
		logerror("%s is not a recognized image type.\n", path);
		return -1;
	}

	logerror("%s: type %s\n", path, img_names[img_type]);

	return ezusb_load_ram(device, path, fx_type, img_type, stage, pre_reset_callback);
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct cannelloni_state *const state = transfer->user_data;

	free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (unsigned int i = 0; i != NUM_TRANSFERS; i++) {
		if (state->transfers[i] == transfer) {
			state->transfers[i] = NULL;
			break;
		}
	}

	state->submitted_transfers--;
}

static void abort_transfers(struct cannelloni_state *state)
{
	state->abort = true;
	for (unsigned int i = 0; i != NUM_TRANSFERS; i++) {
		struct libusb_transfer *const transfer = state->transfers[i];
		if (transfer)
			libusb_cancel_transfer(transfer);
	}
}

static int prepare_out_buffer(struct cannelloni_state *state, uint8_t *buffer)
{
	if (!state->file)
		return state->block_size;

	const uint64_t num_bytes_read = fread(buffer, 1, state->block_size, state->file);
	if (num_bytes_read != state->block_size) {
		if (!feof(stdin))
			logerror("Error reading data from stdin. Stopping.\n");
		else if (verbose)
			logerror("stdin has reached EOF. Stopping.\n");
	}

	// Stop stream after this transfer
	if (num_bytes_read != state->block_size)
		state->total_bytes_left_to_transfer = num_bytes_read;

	return num_bytes_read;
}

static void transfer_complete(struct libusb_transfer *transfer)
{
	struct cannelloni_state *const state = transfer->user_data;

	if (state->abort) {
		free_transfer(transfer);
		return;
	}

	if ((transfer->status != LIBUSB_TRANSFER_COMPLETED && transfer->status != LIBUSB_TRANSFER_TIMED_OUT) ||
		transfer->actual_length == 0) {
		free_transfer(transfer);
		abort_transfers(state);
		return;
	}

	if (state->direction_in &&
		state->file &&
		fwrite(transfer->buffer, 1, transfer->actual_length, state->file) != transfer->actual_length) {
		logerror("Error writing to stdout. Stopping\n");
		free_transfer(transfer);
		abort_transfers(state);
	}

	state->total_bytes_transferred += transfer->actual_length;

	if (!state->direction_in)
		transfer->length = prepare_out_buffer(state, transfer->buffer);

	const int status = libusb_submit_transfer(transfer);
	if (status != LIBUSB_SUCCESS) {
		free_transfer(transfer);
		abort_transfers(state);
	}
}

static struct libusb_transfer* submit_transfer(struct cannelloni_state *state, int timeout)
{
	int num_bytes_to_transfer = state->block_size;

	uint8_t *buf = calloc(state->block_size, 1);
	if (!buf) {
		logerror("Failed to allocate USB transfer buffer.\n");
		goto allocate_transfer_buffer_failed;
	}

	struct libusb_transfer *const transfer = libusb_alloc_transfer(0);
	if (!transfer) {
		logerror("Failed to allocate transfer.\n");
		goto allocate_transfer_failed;
	}

	if (!state->direction_in)
		num_bytes_to_transfer = prepare_out_buffer(state, buf);

	const unsigned char endpoint = state->direction_in ? 0x86 : 0x02; 
	libusb_fill_bulk_transfer(transfer, state->handle, endpoint, buf, num_bytes_to_transfer, transfer_complete,
		(void*)state, timeout);

	const int status = libusb_submit_transfer(transfer);
	if (status != 0) {
		logerror("Failed to submit transfer: %s\n", libusb_error_name(status));
		goto submit_transfer_failed;
	}

	state->total_bytes_left_to_transfer -= num_bytes_to_transfer;
	state->submitted_transfers++;

	return transfer;

submit_transfer_failed:
	libusb_free_transfer(transfer);
allocate_transfer_failed:
	free(buf);
allocate_transfer_buffer_failed:
	return NULL;
}

static size_t count_usb_poll_fds(const struct libusb_pollfd **fds)
{
	size_t num;	
	for (num = 0; fds[num]; num++);
	return num;
}

static sigset_t make_sigset(void)
{
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGINT);
	return sigset;
}

static void handle_signalfd(struct cannelloni_state *state, int signal_poll_fd)
{
	struct signalfd_siginfo info;
	if (read(signal_poll_fd, &info, sizeof(info)) != sizeof(info))
		return;

	if (++(state->num_signals) >= 5) {
		fputs("\nReceived too many signals. Forcibly stopping...\n", stderr);
		exit(-1);
	} else {
		if (verbose)
			fputs("\nSignal received. Stopping...\n", stderr);
		abort_transfers(state);
	}
}

static bool do_stream(libusb_device_handle *handle, bool direction_in, size_t block_size, uint64_t num_bytes_limit,
	bool disable_in_out, unsigned int timeout)
{
	int status;
	uint64_t total_bytes_transferred;
	bool ret = false;
	struct timeval usb_timeout;

	struct cannelloni_state state = {
		.handle = handle,
		.abort = false,
		.num_signals = 0,
		.direction_in = direction_in,
		.file = disable_in_out ? NULL : (direction_in ? stdout : stdin),
		.block_size = block_size,
		.total_bytes_left_to_transfer = num_bytes_limit,
		.total_bytes_transferred = 0,
		.transfers = NULL,
		.submitted_transfers = 0,
		.empty_transfer_count = 0
	};

	// Claim interface 0 again
	status = libusb_claim_interface(handle, 0);
	if (status != LIBUSB_SUCCESS) {
		logerror("libusb_claim_interface failed for data transfer: %s\n", libusb_error_name(status));
		goto claim_interface_failed;
	}

	// Set interface 0 alternate function corresponding to transfer type
	// TODO: Currently only transfer type 1 supported (bulk)
	static const int alternate_function = 1;
	status = libusb_set_interface_alt_setting(handle, 0, alternate_function);
	if (status != LIBUSB_SUCCESS) {
		logerror("libusb_set_interface_alt_setting failed for data transfer: %s\n", libusb_error_name(status));
		goto set_interface_alt_setting_failed;
	}

	// Get polling fds
	const struct libusb_pollfd **const usb_poll_fds = libusb_get_pollfds(NULL);
	if (!usb_poll_fds) {
		logerror("Failed to get pollfd\n");
		goto failed_to_get_usb_poll_fds;
	}

	// Block normal signal handling
	const sigset_t sigset = make_sigset();
	if (sigprocmask(SIG_SETMASK, &sigset, NULL) != 0) {
		logerror("Failed to mask signals\n");
		goto failed_to_mask_signals;
	}

	// Create a signal fd
	const int signal_poll_fd = signalfd(-1, &sigset, 0);
	if (signal_poll_fd == -1) {
		logerror("Failed to open signalfd\n");
		goto failed_to_open_signalfd;
	}

	// Allocate pollfds
	const size_t num_usb_fds = count_usb_poll_fds(usb_poll_fds);
	const size_t num_fds = num_usb_fds + 1;
	struct pollfd *const pollfds = calloc(sizeof(struct pollfd), num_fds);
	if (!pollfds) {
		logerror("Failed to allocate pollfds\n");
		goto failed_to_allocate_pollfds;
	}

	// Construct polling fd list
	for (size_t i = 0; i != num_usb_fds; i++) {
		const struct libusb_pollfd *const usb_poll_fd = usb_poll_fds[i];
		struct pollfd *const fd = pollfds + i;

		fd->fd = usb_poll_fd->fd;
		fd->events = usb_poll_fd->events;
	}

	const size_t signalfd_poll_idx = num_usb_fds;
	pollfds[signalfd_poll_idx] = (struct pollfd){
		.fd = signal_poll_fd,
		.events = POLLIN | POLLERR | POLLHUP
	};

	// Allocate transfers
	if (!(state.transfers = calloc(sizeof(struct libusb_transfer*), NUM_TRANSFERS))) {
		logerror("Failed to allocate transfers.\n");
		goto allocate_transfers_array_failed;
	}

	for (unsigned int i = 0; i != NUM_TRANSFERS && state.total_bytes_left_to_transfer != 0; i++) {
		if (!(state.transfers[i] = submit_transfer(&state, timeout))) {
			abort_transfers(&state);
			break;
		}
	}

	// Poll transfers
	const uint64_t time0 = get_time();

	while (state.submitted_transfers != 0) {
		if ((status = libusb_get_next_timeout(NULL, &usb_timeout)) < 0) {
			logerror("Failed to check USB timeout: %d\n", status);
			break;
		}

		const int poll_timeout = (status == 0) ? MAX_POLL_TIMEOUT :
			(usb_timeout.tv_sec * 1000 + usb_timeout.tv_usec / 1000);
		if ((status = poll(pollfds, num_fds, poll_timeout)) == -1)
			break;

		libusb_handle_events_timeout(NULL, &(struct timeval){0, 0});

		if (pollfds[signalfd_poll_idx].revents & POLLIN)
			handle_signalfd(&state, signal_poll_fd);
	}

	const uint64_t time1 = get_time();

	// Print result statistics
	if (verbose && !state.abort) {
		// Seconds
		const float delta_time = (time1 - time0) * 1e-9f;

		// MiB/s
		const float speed = total_bytes_transferred / (delta_time * 1024.f * 1024.f);

		fprintf(stderr, "Transferred %"PRIu64" bytes in %.2f seconds (%.2f MiB/s)\n",
			total_bytes_transferred, delta_time, speed);

	}

	ret = true;

	free(state.transfers);
allocate_transfers_array_failed:
	free(pollfds);
failed_to_allocate_pollfds:
	close(signal_poll_fd);
failed_to_open_signalfd:
failed_to_mask_signals:
	libusb_free_pollfds(usb_poll_fds);
failed_to_get_usb_poll_fds:
set_interface_alt_setting_failed:
	libusb_release_interface(handle, 0);
claim_interface_failed:

	return ret;
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
	int fx_type = FX_TYPE_UNDEFINED, img_type[ARRAYSIZE(path)];

	int opt, status;
	unsigned int i, j;
	unsigned vid = 0, pid = 0;
	unsigned busnum = 0, devaddr = 0, _busnum, _devaddr;
	libusb_device *dev, **devs;
	libusb_device_handle *device = NULL;
	struct libusb_device_descriptor desc;

	bool direction_in = true;
	bool disable_in_out = false;
	bool use_8bit_bus = false;
	int num_buffers = 4;
	bool run_async_bus = false;

	int block_size = 16384;
	unsigned int bSize = 0;
	uint64_t num_bytes_limit = UNLIMITED;

	bool use_external_ifclk = false;
	bool use_48mhz_internal_clk = true;
	bool enable_ifclk_output = false;
	bool invert_ifclk = false;

	int cpu_mhz = MHZ48;
	bool enable_clkout_output = false;
	bool invert_clkout = false;

	bool invert_queue_full_pin = false;
	bool invert_queue_empty_pin = false;
	bool invert_queue_slwr_pin = false;
	bool invert_queue_slrd_pin = false;
	bool invert_queue_sloe_pin = false;
	bool invert_queue_pktend_pin = false;

	// Parse arguments

	while ((opt = getopt(argc, argv, "qvV?hiow80432aseljkrxz:b:d:p:f:g:t:n:c:")) != EOF)

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
			direction_in = true;
			break;

		case 'o':
			direction_in = false;
			break;

		case '0':
			disable_in_out = true;
			break;

		case 'w':
			use_8bit_bus = false;
			break;

		case '8':
			use_8bit_bus = true;
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
			run_async_bus = true;
			break;

		case 's':
			run_async_bus = false;
			break;

		case 'b':
			if (sscanf(optarg, "%u" , &bSize) != 1 || bSize < 2 || bSize & 1) {
				fputs ("-b: Please specify a positive, even buffer size in bytes in decimal format.", stderr);
				return -1;
			}
			block_size = bSize;
			break;

		case 'n':
			if (sscanf(optarg, "%"PRIu64, &num_bytes_limit) != 1 || num_bytes_limit < 2 || num_bytes_limit & 1) {
				fputs ("-n: Please specify a positive, even number of bytes in decimal format.", stderr);
				return -1;
			}
			break;

		case 'c':
			if (parse_option_c(optarg, &use_external_ifclk, &use_48mhz_internal_clk, &enable_ifclk_output, &invert_ifclk)) {
				return print_usage(-1);
			}
			break;

		case 'z':
			if (parse_option_z(optarg, &cpu_mhz, &enable_clkout_output, &invert_clkout)) {
				return print_usage(-1);
			}
			break;

		case 'e':
			invert_queue_empty_pin = true;
			break;

		case 'l':
			invert_queue_full_pin = true;
			break;

		case 'x':
			invert_queue_slwr_pin = true;
			break;

		case 'r':
			invert_queue_slrd_pin = true;
			break;

		case 'j':
			invert_queue_sloe_pin = true;
			break;

		case 'k':
			invert_queue_pktend_pin = true;
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

	if (num_bytes_limit != UNLIMITED && num_bytes_limit % block_size != 0) {
		logerror("Number of bytes to transfer must be divisible by buffer size.\n");
		return print_usage(-1);
	}

	if (!use_8bit_bus && (block_size & 1)) {
		logerror("When using 16-bit wide bus (option '-w'), blockSize (option '-b') must be even.\n");
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
	if (use_8bit_bus) firmware_config[ 3 ] &= 0xFE;

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

	// Program the firmware in the microcontroller
	if (!path[LOADER]) {
		// Single stage, put into internal memory
		if (verbose > 1)
			logerror("single stage: load on-chip memory\n");
		status = load_image(device, path[FIRMWARE], fx_type, 0, pre_reset_callback);
	} else {
		// two-stage, put loader into external memory
		if (verbose > 1)
			logerror("1st stage: load 2nd stage loader\n");
		status = load_image(device, path[LOADER], fx_type, 0, pre_reset_callback);
		if (status == 0) {
			// two-stage, put firmware into internal memory
			if (verbose > 1)
				logerror("2nd state: load on-chip memory\n");
			status = load_image(device, path[FIRMWARE], fx_type, 1, NULL);
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

	// Do the transfer
	status = do_stream(device, direction_in, block_size, num_bytes_limit, disable_in_out, 1000) ?
		EXIT_SUCCESS : EXIT_FAILURE;

	// Close device
	libusb_close(device);

	// Exit libusb
	libusb_exit(NULL);

	return status;

	err:
	libusb_exit(NULL);
	return -1;
}
