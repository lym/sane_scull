# Test the scull driver by simulating/excersizing all it device operations
# then add a make test target to the main Makefile that invokes this one.
#
# Testing Procedure:
#  - Make a list of requirements (features to be provided)
#  - Build a basic device skeleton
#  - Load the device
#  - Using a coreutil, run a failing op against the device
#  - Implement the op in code
#  - Run the formally failing feature to see that it succeeds.
#
# Why Test?
#  - To be sure the driver does what it's supposed to do, in the first place.
#  - To confidently update the code when the kernel module API is evolved.


ioctl_test : ioctl_test.o
	cc -o ioctl_test ioctl_test.o

ioctl_test.o : ioctl_test.c
