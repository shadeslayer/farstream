
all: test-bare test

CFLAGS = -g -Wextra -Wall -Wno-unused-parameter -Wno-missing-field-initializers `pkg-config --cflags gupnp-1.0`
LDFLAGS =  `pkg-config --libs gupnp-1.0`

test-bare: test-bare.o

test: fs-upnp-simple-igd.o test.o

clean:
	rm -f *.o test test-bare
