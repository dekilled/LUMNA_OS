/* lumna-reboot [bootloader|recovery]: reinicia passando o modo pro bootloader.
 * O reboot do busybox não aceita argumento, e sem ele não dá pra cair no
 * fastboot sem os botões. Sem libc: só as syscalls sync e reboot do arm64. */
#define SYS_sync 81
#define SYS_reboot 142
#define MAGIC1 0xfee1dead
#define MAGIC2 672274793
#define CMD_RESTART2 0xA1B2C3D4

static long sys(long n, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
	return x0;
}

__attribute__((noreturn)) void _start_c(long *sp)
{
	char *mode = sp[0] > 1 ? (char *)sp[2] : "";
	sys(SYS_sync, 0, 0, 0, 0);
	sys(SYS_reboot, MAGIC1, MAGIC2, CMD_RESTART2, (long)mode);
	for (;;)
		sys(93, 1, 0, 0, 0); /* exit(1) se o reboot falhar */
}

__asm__(".global _start\n_start:\n mov x0, sp\n b _start_c\n");
