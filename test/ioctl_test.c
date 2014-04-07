/*
 * Test scull's ioctl functionality
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

//#include "/home/lym/projects/c_projects/dev_drivers/scull/scull.h"
#define SCULL_IOC_MAGIC	0x81		/* 00-0C */
#define SCULL_IOCQQSET		_IO(SCULL_IOC_MAGIC,	8)

int ioctl(int fd, unsigned long request, ...);

int ioctl_test(void)
{
	int fd, res;
	printf("Testing scull driver's ioctl functionality........\n");

	fd = open("/dev/scull0", O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		perror("Unable to open device");
		return 1;
	}

	res = ioctl(fd, SCULL_IOCQQSET);
	if (res < 0)
		perror("Call Unsuccessful");
	else
		printf("[Scull Query]: Works fine\n");

	return 0;
}

int main(void)
{
	int t;
	t = ioctl_test();

	return t;
}
