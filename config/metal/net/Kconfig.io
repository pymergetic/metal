menu "io"

config PM_METAL_IO_WIRE_MAX_KIB
	int "PM_METAL_IO_WIRE_MAX (KiB)"
	default 1024
	range 4 4096
	help
	  TLS/HTTP-client/py-recv wire chunk. Also seeds /limits net.PM_METAL_IO_WIRE_MAX.
	  confgen emits CONFIG_PM_METAL_IO_WIRE_MAX in bytes.

endmenu
