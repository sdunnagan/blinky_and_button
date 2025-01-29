#------------------------------------------------------------
# File:         Makefile
#
# Description:  Builds blinky app for RPi 4.
#------------------------------------------------------------

CC = gcc
TARGET = blinky
LDFLAGS = -lpthread -lgpiod
CCFLAGS = -g -Wall
INC_PATH = -I
LIBS_PATH = -L

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
INCLUDES = $(wildcard *.h)

%.o: %.c $(INCLUDES)
	$(CC) $(CCFLAGS) $(INC_PATH) $(LIBS_PATH) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LDFLAGS) $(INC_PATH) $(LIBS_PATH) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)
