Very Simple implementation of Scull Pipe device driver

Refernce: LDD3

Usage:
	
	o $ make
	
	o $ insmod skull.ko

	o $ cat /proc/devinfo	

 	o $ Not the major number of device "scull-pipe"

	o mknod /dev/scullp c <major number> <minor number>	
	  This will create character file /dev/scullp

	o Info of device can be seen in scullseq
