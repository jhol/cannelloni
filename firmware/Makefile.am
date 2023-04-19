##
## This file is part of the cannelloni project.
##
## Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
## Copyright (C) 2015 Daniel Elstner <daniel.kitta@gmail.com>
## Copyright (C) 2023 NVIDIA Corporation <jholdsworth@nvidia.com>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, see <http://www.gnu.org/licenses/>.
##

GNUMAKEFLAGS = --no-print-directory
SUFFIXES = .c .ihx .rel

# Flags for firmware hex file generation
SDCC_LINK_FLAGS =

# Include paths
c_includes = -I$(srcdir)/include

# Final firmware file
firmware_binary = fx2pipe.ihx

fx2pipe_sources = \
	src/fx2pipe.c

fx2pipe_headers = \
	include/fx2regs.h

fx2pipe_objects = \
	src/fx2pipe.rel

firmwaredir = $(datadir)/cannelloni
firmware_DATA = $(firmware_binary)

dist_noinst_DATA = \
	$(fx2pipe_headers) $(fx2pipe_sources)

MOSTLYCLEANFILES = *.asm
CLEANFILES = $(firmware_binary)

# Custom silent rules prefixes
SF_V_SDCC = $(sf_v_SDCC_@AM_V@)
sf_v_SDCC_ = $(sf_v_SDCC_@AM_DEFAULT_V@)
sf_v_SDCC_0 = @echo "  SDCC    " $@;
sf_v_SDCC_1 =

all-local: $(firmware_binary)

mostlyclean-local:
	-$(AM_V_at)find . -type f '(' -name '*.ihx' -o -name '*.lk' \
		-o -name '*.lst' -o -name '*.map' -o -name '*.mem' \
		-o -name '*.rel' -o -name '*.rst' -o -name '*.sym' ')' \
		-exec rm -f '{}' '+'

# List dependencies
$(fx2pipe_objects): $(fx2pipe_headers)

$(firmware_binary): $(fx2pipe_objects)
	$(AM_V_GEN)$(SDCC) -mmcs51 $(SDCC_LINK_FLAGS) -o $@ $(fx2pipe_objects)

.c.rel:
	$(SF_V_SDCC)$(SDCC) -mmcs51 $(c_includes) -c $< -o $@
