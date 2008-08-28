
all: test obj-test

CFLAGS = -Wextra -Wall -Wno-unused-parameter -Wno-missing-field-initializers `pkg-config --cflags gupnp-1.0`
LDFLAGS =  `pkg-config --libs gupnp-1.0`

test: test.o

obj-test: fs-upnp-simple-igd.o
