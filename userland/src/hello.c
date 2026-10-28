/*
 * First user-space program for the Darwin-on-redwood bring-up.
 *
 * A static arm64 Mach-O with no libc and no dyld: it talks to the kernel with
 * raw system calls. The kernel's mockfs presents the ramdisk (this very file)
 * as /sbin/launchd, so this runs as process 1.
 *
 * Built with -DHOLD (prebuilt/userland/hold) it does not exit but sleeps, so that the kernel runs on
 * until loader_return_after: for drivers that need longer than a moment (touchscreen=1).
 */
typedef unsigned long u64;

/* XNU arm64 system call: number in x16, arguments in x0.., "svc #0x80"; carry set on error */
static long
syscall3(long num, long a, long b, long c)
{
	register long x16 __asm__("x16") = num;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	__asm__ volatile ("svc #0x80" : "+r" (x0) : "r" (x16), "r" (x1), "r" (x2) : "memory", "cc");
	return x0;
}

#define SYS_exit    1
#define SYS_select  93
#define SYS_write   4
#define SYS_open    5
#define SYS_getpid  20
#define SYS_getuid  24
#define O_RDWR      2

static unsigned
length(const char *s)
{
	unsigned n = 0;
	while (s[n]) {
		n++;
	}
	return n;
}

static void
put(int fd, const char *s)
{
	syscall3(SYS_write, fd, (long)s, length(s));
}

static void
put_number(int fd, long v)
{
	char buf[24];
	int i = sizeof(buf);
	buf[--i] = 0;
	do {
		buf[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	put(fd, buf + i);
}

void
start(void)
{
	int fd = (int)syscall3(SYS_open, (long)"/dev/console", O_RDWR, 0);

	put(fd, "\nHello from user space on Darwin!\n");
	put(fd, "I am process ");
	put_number(fd, syscall3(SYS_getpid, 0, 0, 0));
	put(fd, ", uid ");
	put_number(fd, syscall3(SYS_getuid, 0, 0, 0));
	put(fd, ", console fd ");
	put_number(fd, fd);
	put(fd, ".\n");

#ifdef HOLD
	for (;;) {
		long timeout[2] = { 3600, 0 };          /* struct timeval: an hour, then again */
		register long x16 __asm__("x16") = SYS_select;
		register long x0 __asm__("x0") = 0;
		register long x1 __asm__("x1") = 0, x2 __asm__("x2") = 0, x3 __asm__("x3") = 0;
		register long x4 __asm__("x4") = (long)timeout;

		__asm__ volatile ("svc #0x80" : "+r" (x0) : "r" (x16), "r" (x1), "r" (x2), "r" (x3), "r" (x4) : "memory", "cc");
	}
#endif
	/* Process 1 is not supposed to exit; doing so panics the kernel, which ends the test run. */
	syscall3(SYS_exit, 0, 0, 0);
	for (;;) {
	}
}
