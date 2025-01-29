#-----------------------------------------------------------------
# Makefile
#
# Builds my GPIO driver and device tree overlay.
#-----------------------------------------------------------------

obj-m := gpio_button.o

PWD := $(shell pwd)

modules:
	$(MAKE) -C '$(LINUX_DIR)' M='$(PWD)' modules

clean:
	$(MAKE) -C '$(LINUX_DIR)' M='$(PWD)' clean
