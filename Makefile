
COMPILER = gcc
BUILD_TYPE = -o3
#BUILD_TYPE = -g

INCLUDE_DIR = -I"src/"
SRC_FILES = src/ezusb.c src/cannelloni.c
LIBS_DIRS =
LIBS = -lm -lusb-1.0

#------

THE_PROGRAM = cannelloni

cannelloni: clean
	${COMPILER} ${BUILD_TYPE} ${LIBS_DIRS} ${INCLUDE_DIR} ${SRC_FILES} -o ${THE_PROGRAM} ${LIBS}

clean:
	rm ${THE_PROGRAM} || "true"
