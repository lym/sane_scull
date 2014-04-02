/*
 * Test scull's ioctl functionality
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "/home/lym/projects/c_projects/dev_drivers/scull/scull.h"

int ioctl(int fd, unsigned long request, ...);

int ioctl_test(void)
{
	int fd, res;
	printf("Testing scull driver's ioctl functionality........");

	fd = open("/dev/scull0", O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		perror("Unable to open device");
		return 1;
	}

	res = ioctl(fd, SCULL_IOCQQSET);
	if (res < 0)
		perror("Call Unsuccessful");
	else
		printf("[Scull Query]: Works fine");

	return 0;
}
