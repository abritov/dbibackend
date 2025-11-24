CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lusb-1.0
TARGET = dbibackend
SRC = dbibackend.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: all clean install
