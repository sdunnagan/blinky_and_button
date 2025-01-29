#------------------------------------------------------------
# File:         Makefile
#
# Description:  Builds button app.
#------------------------------------------------------------

CC = gcc
TARGET = button
LDFLAGS = -lpthread -lgpiod
CCFLAGS = -g -Wall
INCLUDES_PATH = -I
LIBS_PATH = -L

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
INCLUDES = $(wildcard *.h)

%.o: %.c $(INCLUDES)
	$(CC) $(CCFLAGS) $(INCLUDES_PATH) $(LIBS_PATH) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LDFLAGS) $(INCLUDES_PATH) $(LIBS_PATH) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)
