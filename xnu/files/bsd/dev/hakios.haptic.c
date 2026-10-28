/*
 * The vibration motor of the POCO X5 Pro (redwood): a linear resonant actuator driven by an
 * Awinic AW86927 (chip ID 0x9270), Android's "haptic_hv" (Xiaomi's driver aw8692x, version 1.7.0).
 * Every address and number below belongs to this phone, from Android's own device tree, its
 * kernel's log and the chip's registers as Android leaves them (read from sysfs, read-only):
 *
 *   I2C        the serial engine 9 (qupv3_se9_i2c at 0xa84000, function qup11 on GPIO 36 data and
 *              37 clock, internal pull-ups, 400 kHz under Android), address 0x5a; done by hand here
 *              as for the amplifiers (hakios.speaker.c), the two pins taken for each transaction
 *              and given back as they were found
 *   reset      GPIO 35, high = running (Android: low for 5 ms, then high, at its driver's start)
 *   interrupt  GPIO 18, pulled up, low when the chip has something to say (not used: polled)
 *   the motor  f0 175.5 Hz as Android calibrated it (f0_pre 170.0 Hz, calibration data 0x0d)
 *   effects    Android's RAM waveforms, /vendor/firmware/aw8697_haptic.bin of the phone (fw-haptic/
 *              here, not in the repository; userland/build.sh puts it into /fw of the system image):
 *              eleven of them, 1..10 short (Android's effects 0..9), 11 the one looped for a
 *              vibration of some length
 *
 * Nothing here is done unless a boot-arg asks for it:
 *
 *   haptic=1   a look, reading only: the pins, and whether the chip answers (its ID and registers,
 *              against what Android leaves in them). Out of Android's bootloader the reset pin is an
 *              input with a pull-down: the chip is held in reset and does not answer.
 *   haptic=2   ... and the reset released as Android's driver releases it (low 5 ms, then high; it
 *              stays released until the run ends), and the chip's registers read as they come out
 *              of reset, against Android's. Nothing is written to the chip.
 *   haptic=3   ... and Android's configuration written (every write read back), Android's
 *              waveforms loaded into its RAM and read back; then standby. Nothing moves.
 *   haptic=4   ... and one short effect played (haptic_effect=N, default 1: Android's effect 0)
 *   haptic=5   ... configured and loaded as for 3, and instead of the effect the motor for iOS until
 *              the run ends: what iOS plays on its "Actuator" device (the HAL plug-in's,
 *              userland/src/audio_plugin.c: the system's feedbacks and vibrations, CoreHaptics'
 *              patterns, UIKit's feedbacks) is written to /dev/hakios-haptic as samples of one
 *              channel, and the motor plays Android's waveforms by it: each waveform of iOS's (a
 *              stretch of samples after a pause) a short tap (waveform 1), and one that goes on for
 *              longer than a tap the waveform Android loops for vibrations (11), until it ends -- or
 *              for HAPTIC_MAX_MS at most, after which the motor rests for as long before it plays
 *              again. The gain follows how loud iOS's waveform is: Android's own (0x80) from a third
 *              of full scale up, five eighths of it for the softest, never above it (gain_for()).
 *
 * Limits, whatever asks: Android's own values only (its gain 0x80, its boost and drive levels, its
 * waveforms); no effect longer than HAPTIC_MAX_MS, and the chip is told to stop and put into
 * standby when that is over whether it has stopped or not; any fault the chip reports (under-
 * voltage, over-current, over-temperature, the boost's short circuit or over-voltage), a write that
 * does not read back or a byte nobody acknowledges, and the chip is put into reset and nothing more
 * is done for the rest of the run. At the run's end (and on a panic's way out) the chip goes back
 * into reset. Nothing is written anywhere but to the chip's registers and its RAM.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/hakios.audio.h>
#include <sys/hakios.input.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
extern void loader_log_putc(char c);

static vm_offset_t tlmm;
static uint32_t level;
static bool broken, released, leaving;

/* every line to the loader's log as well (and to the console when that may be written) */
static int say(const char *format, ...) __printflike(1, 2);

static int
say(const char *format, ...)
{
	char line[300];
	va_list list;
	int length;

	va_start(list, format);
	length = vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	for (const char *c = line; *c != 0; c++) {
		loader_log_putc(*c);
	}
	if (!leaving && ml_get_interrupts_enabled()) {
		printf("%s", line);
	}
	return length;
}

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
wr(vm_offset_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

static void
spin_us(uint32_t us)
{
	uint64_t until;

	nanoseconds_to_absolutetime((uint64_t)us * NSEC_PER_USEC, &until);
	until += mach_absolute_time();
	while (mach_absolute_time() < until) {
	}
}

static void
wait_ms(uint32_t ms)
{
	if (leaving) {
		spin_us(ms * 1000);
	} else {
		delay_for_interval(ms, NSEC_PER_MSEC);
	}
}

/* ---- pins ------------------------------------------------------------------------------------------ */

#define TLMM_PHYS               0x0f100000UL
#define TLMM_SIZE               0x100000UL
#define GPIO_CFG(n)             (tlmm + 0x1000UL * (n))         /* [1:0] pull, [5:2] function, [8:6] drive, [9] output */
#define GPIO_IO(n)              (tlmm + 0x1000UL * (n) + 4)     /* bit 0 in, bit 1 out */
#define CFG_PULL_UP             3
#define CFG_OUTPUT              (1u << 9)

#define PIN_SDA                 36      /* the serial engine's first pin is the data line, the second the clock */
#define PIN_SCL                 37
#define PIN_RESET               35
#define PIN_INT                 18

static void
show_pins(void)
{
	static const struct {
		unsigned pin;
		const char *what;
	} pins[] = { { PIN_SDA, "I2C data" }, { PIN_SCL, "I2C clock" }, { PIN_RESET, "reset" }, { PIN_INT, "interrupt" } };

	for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		uint32_t cfg = rd(GPIO_CFG(pins[i].pin)), io = rd(GPIO_IO(pins[i].pin));

		say("haptic: gpio%u (%s): function %u, pull %u, %u mA, %s, reads %u\n", pins[i].pin, pins[i].what, (cfg >> 2) & 15,
		    cfg & 3, (((cfg >> 6) & 7) + 1) * 2, (cfg & CFG_OUTPUT) ? (io & 2 ? "drives high" : "drives low") : "input", io & 1);
	}
}

/* the reset pin as an output of the GPIO function, driving `high' (2 mA, no pull, as Android has it) */
static void
reset_pin(bool high)
{
	wr(GPIO_IO(PIN_RESET), high ? 2 : 0);
	wr(GPIO_CFG(PIN_RESET), CFG_OUTPUT);
	released = high;
}

/* ---- I2C by hand (as in hakios.speaker.c) ---------------------------------------------------------- */

static void
half_bit(void)
{
	spin_us(5);                     /* 100 kHz at most */
}

static void
line(unsigned pin, bool high)
{
	wr(GPIO_IO(pin), 0);
	wr(GPIO_CFG(pin), CFG_PULL_UP | (high ? 0 : CFG_OUTPUT));
}

static bool
level_of(unsigned pin)
{
	return rd(GPIO_IO(pin)) & 1;
}

static void
i2c_start(void)
{
	line(PIN_SDA, true);
	line(PIN_SCL, true);
	half_bit();
	line(PIN_SDA, false);
	half_bit();
	line(PIN_SCL, false);
	half_bit();
}

static void
i2c_stop(void)
{
	line(PIN_SDA, false);
	half_bit();
	line(PIN_SCL, true);
	half_bit();
	line(PIN_SDA, true);
	half_bit();
}

static bool
i2c_bit(bool out)
{
	bool in;

	line(PIN_SDA, out);
	half_bit();
	line(PIN_SCL, true);
	for (unsigned stretched = 0; !level_of(PIN_SCL) && stretched < 100; stretched++) {
		half_bit();
	}
	half_bit();
	in = level_of(PIN_SDA);
	line(PIN_SCL, false);
	return in;
}

static bool
i2c_write(uint8_t byte)
{
	for (int bit = 7; bit >= 0; bit--) {
		i2c_bit((byte >> bit) & 1);
	}
	return !i2c_bit(true);
}

static uint8_t
i2c_read(bool more)
{
	uint8_t byte = 0;

	for (int bit = 7; bit >= 0; bit--) {
		byte = (uint8_t)(byte << 1 | i2c_bit(true));
	}
	i2c_bit(!more);
	return byte;
}

static uint32_t taken_cfg[2], taken_io[2];

static bool
bus_take(void)
{
	taken_cfg[0] = rd(GPIO_CFG(PIN_SDA));
	taken_cfg[1] = rd(GPIO_CFG(PIN_SCL));
	taken_io[0] = rd(GPIO_IO(PIN_SDA));
	taken_io[1] = rd(GPIO_IO(PIN_SCL));
	line(PIN_SDA, true);
	line(PIN_SCL, true);
	half_bit();
	if (!level_of(PIN_SDA) || !level_of(PIN_SCL)) {
		wr(GPIO_IO(PIN_SDA), taken_io[0] & 2);
		wr(GPIO_IO(PIN_SCL), taken_io[1] & 2);
		wr(GPIO_CFG(PIN_SDA), taken_cfg[0]);
		wr(GPIO_CFG(PIN_SCL), taken_cfg[1]);
		say("haptic: the I2C bus is not at rest (gpio%u reads %u, gpio%u reads %u): not touched\n", PIN_SDA,
		    level_of(PIN_SDA), PIN_SCL, level_of(PIN_SCL));
		return false;
	}
	return true;
}

static void
bus_give_back(void)
{
	wr(GPIO_IO(PIN_SDA), taken_io[0] & 2);
	wr(GPIO_IO(PIN_SCL), taken_io[1] & 2);
	wr(GPIO_CFG(PIN_SDA), taken_cfg[0]);
	wr(GPIO_CFG(PIN_SCL), taken_cfg[1]);
}

#define AW_ADDRESS              0x5a

/* `count' 8-bit registers from `reg' on (the chip's address counts up, but for its RAM data port) */
static bool
regs_read(uint8_t reg, uint8_t *values, unsigned count)
{
	bool answered;

	if (!bus_take()) {
		return false;
	}
	i2c_start();
	answered = i2c_write(AW_ADDRESS << 1) && i2c_write(reg);
	if (answered) {
		i2c_start();
		answered = i2c_write(AW_ADDRESS << 1 | 1);
	}
	for (unsigned i = 0; answered && i < count; i++) {
		values[i] = i2c_read(i + 1 < count);
	}
	i2c_stop();
	bus_give_back();
	return answered;
}

static bool
regs_write(uint8_t reg, const uint8_t *values, unsigned count)
{
	bool answered;

	if (!bus_take()) {
		return false;
	}
	i2c_start();
	answered = i2c_write(AW_ADDRESS << 1) && i2c_write(reg);
	for (unsigned i = 0; answered && i < count; i++) {
		answered = i2c_write(values[i]);
	}
	i2c_stop();
	bus_give_back();
	return answered;
}

/* ---- the chip -------------------------------------------------------------------------------------- */

#define AW_RSTCFG               0x00            /* 0xaa: a soft reset */
#define AW_SYSST                0x01
#define AW_SYSINT               0x02            /* read: cleared */
#define AW_SYSINTM              0x03
#define AW_PLAYCFG1             0x06            /* [7] boost on, [6:0] its voltage */
#define AW_PLAYCFG2             0x07            /* the gain */
#define AW_PLAYCFG3             0x08            /* [1:0] the play mode: 0 RAM */
#define AW_PLAYCFG4             0x09            /* [1] stop, [0] go */
#define AW_WAVCFG1              0x0a            /* the first waveform to play, 1..127 (0: the end) */
#define AW_WAVCFG2              0x0b
#define AW_WAVCFG9              0x12            /* [7:4] how often the first is repeated, 15: until told to stop */
#define AW_BASEADDRH            0x2d
#define AW_BASEADDRL            0x2e
#define AW_GLBRD5               0x3f            /* [3:0] the state: 0 standby */
#define AW_RAMADDRH             0x40
#define AW_RAMADDRL             0x41
#define AW_RAMDATA              0x42
#define AW_SYSCTRL3             0x45            /* [5] standby, [2] the RAM open for loading */
#define AW_TMCFG                0x5b            /* 0x7d opens the analog registers 0x70.. for writing, 0 closes them */
#define AW_TRIM_LRA             0x79            /* the motor's f0 calibration */
#define AW_CHIPIDH              0x57
#define AW_CHIPIDL              0x58
#define AW_CHIPID               0x9270

#define SYSINT_FAULTS           0xe6            /* boost short circuit and over-voltage, under-voltage, over-current, over-temperature */
#define HAPTIC_MAX_MS           1000

static void
give_up(const char *why)
{
	broken = true;
	say("haptic: %s: THE CHIP IS PUT INTO RESET (gpio%u low) and nothing more is done\n", why, PIN_RESET);
	if (tlmm != 0) {
		reset_pin(false);
	}
}

static bool
get(uint8_t reg, uint8_t *value)
{
	if (!regs_read(reg, value, 1)) {
		say("haptic: %#04x NOT READ\n", reg);
		return false;
	}
	return true;
}

/* One register written and read back. */
static bool
put(uint8_t reg, uint8_t value)
{
	uint8_t after = 0;

	if (broken) {
		return false;
	}
	if (!regs_write(reg, &value, 1)) {
		say("haptic: %#04x <- %#04x NOT ACKNOWLEDGED\n", reg, value);
		give_up("a write was not acknowledged");
		return false;
	}
	if (!regs_read(reg, &after, 1)) {
		give_up("a register could not be read back");
		return false;
	}
	say("haptic: %#04x <- %#04x, reads %#04x%s\n", reg, value, after, after == value ? "" : ": NOT AS WRITTEN");
	if (after != value) {
		give_up("a register did not read back as written");
		return false;
	}
	return true;
}

/* The chip's interrupt flags (reading clears them): false, and the chip given up on, if one is a fault. */
static bool
healthy(const char *when)
{
	uint8_t flags = 0;

	if (!get(AW_SYSINT, &flags)) {
		give_up("the flags could not be read");
		return false;
	}
	if (flags & SYSINT_FAULTS) {
		say("haptic: %s: the chip reports %#04x:%s%s%s%s%s\n", when, flags, flags & 0x80 ? " BOOST SHORT CIRCUIT" : "",
		    flags & 0x40 ? " BOOST OVER-VOLTAGE" : "", flags & 0x20 ? " UNDER-VOLTAGE" : "", flags & 0x04 ? " OVER-CURRENT" : "",
		    flags & 0x02 ? " OVER-TEMPERATURE" : "");
		give_up("a fault");
		return false;
	}
	return true;
}

/* Android's registers as its driver leaves them, read from the phone's sysfs (fw-haptic/android/haptic-sysfs.txt) */
static const uint8_t android_regs[0x7d] = {
	[0x00] = 0x82, [0x01] = 0x10, [0x02] = 0x11, [0x03] = 0x59, [0x04] = 0x09, [0x05] = 0x00, [0x06] = 0x47, [0x07] = 0x80,
	[0x08] = 0x07, [0x09] = 0x00, [0x0a] = 0x0b, [0x12] = 0xf0, [0x18] = 0x1e, [0x19] = 0x8d, [0x1a] = 0x71, [0x1b] = 0x06,
	[0x1c] = 0x58, [0x1d] = 0xcf, [0x1e] = 0x28, [0x1f] = 0x04, [0x20] = 0x06, [0x21] = 0x08, [0x22] = 0x0c, [0x23] = 0x0c,
	[0x24] = 0x62, [0x25] = 0x08, [0x26] = 0xa0, [0x27] = 0x08, [0x28] = 0x8c, [0x2d] = 0x08, [0x2f] = 0x46, [0x33] = 0x01,
	[0x34] = 0x01, [0x35] = 0x01, [0x36] = 0x02, [0x37] = 0x02, [0x38] = 0x02, [0x3b] = 0x05, [0x3c] = 0x08, [0x3e] = 0x1b,
	[0x40] = 0x08, [0x41] = 0x03, [0x45] = 0x12, [0x46] = 0x09, [0x47] = 0x5a, [0x49] = 0x28, [0x4a] = 0xbf, [0x4b] = 0x32,
	[0x4d] = 0x24, [0x4e] = 0x04, [0x4f] = 0x0a, [0x51] = 0x07, [0x57] = 0x92, [0x58] = 0x70, [0x59] = 0x04, [0x5a] = 0x98,
	[0x5d] = 0xf5, [0x5e] = 0x0d, [0x5f] = 0xf5, [0x60] = 0x41, [0x61] = 0xf6, [0x62] = 0x5c, [0x63] = 0x0e, [0x64] = 0xf9,
	[0x68] = 0x05, [0x69] = 0x21, [0x6b] = 0x02, [0x6c] = 0x15, [0x6d] = 0x89, [0x6e] = 0xc2, [0x6f] = 0x48, [0x70] = 0x0f,
	[0x71] = 0x93, [0x72] = 0x69, [0x73] = 0xa4, [0x74] = 0x89, [0x75] = 0x88, [0x77] = 0x40, [0x79] = 0x0d,
};

/* What the chip holds now, against Android's (registers 0x00..0x7c; 0x32 and 0x42 are data ports, not read). */
static void
show_registers(void)
{
	unsigned differ = 0;

	for (unsigned reg = 0; reg <= 0x7c; reg++) {
		uint8_t value = 0;

		if (reg == 0x32 || reg == 0x42) {
			continue;
		}
		if (!regs_read((uint8_t)reg, &value, 1)) {
			say("haptic: %#04x NOT READ\n", reg);
			return;
		}
		if (value != android_regs[reg]) {
			differ++;
			say("haptic: %#04x = %#04x (Android leaves %#04x)\n", reg, value, android_regs[reg]);
		}
	}
	say("haptic: %u registers differ from what Android leaves in them\n", differ);
}

static bool
chip_answers(void)
{
	uint8_t id[2] = { 0, 0 };

	if (!regs_read(AW_CHIPIDH, id, 2)) {
		say("haptic: nobody answers at %#x on the bus of gpio%u/%u\n", AW_ADDRESS, PIN_SDA, PIN_SCL);
		return false;
	}
	say("haptic: the chip at %#x is %#06x%s\n", AW_ADDRESS, id[0] << 8 | id[1], (id[0] << 8 | id[1]) == AW_CHIPID ? ", an AW86927" :
	    ": NOT THE AW86927 EXPECTED");
	return (id[0] << 8 | id[1]) == AW_CHIPID;
}

/*
 * Android's configuration: the registers its driver sets (and the mainline Linux driver aw86927.c sets the
 * same way), with Android's values -- those that differ from the chip's own after a reset, as the phone
 * showed (haptic=2). Not written: the status and interrupt flags, the RAM's address and data ports, the
 * results of the chip's measurements (0x25..0x28: they do not take what is written, they are what its f0
 * tracking found under Android; 0x4f, 0x51, 0x59, 0x5a likewise), and what is set for each playing (0x08 the mode,
 * 0x0a/0x12 the waveform and its repeats). The values are all read from the chip as Android leaves it.
 */
static const uint8_t android_config[][2] = {
	{ 0x03, 0x59 },         /* the interrupt mask */
	{ 0x06, 0x47 },         /* PLAYCFG1: the boost in bypass, its voltage setting 0x47 (Android's vmax 7938 mV) */
	{ 0x1a, 0x71 },         /* CONTCFG3 */
	{ 0x1d, 0xcf },         /* CONTCFG6: f0 tracking on, the first drive level 0x4f */
	{ 0x1e, 0x28 },         /* CONTCFG7: the second drive level */
	{ 0x2f, 0x46 },         /* the FIFO's thresholds */
	{ 0x36, 0x02 }, { 0x37, 0x02 }, { 0x38, 0x02 },  /* the trigger pins' waveforms (the triggers stay off) */
	{ 0x39, 0x00 }, { 0x3a, 0x00 },  /* ... and off */
	{ 0x46, 0x09 },         /* SYSCTRL4: 24 kHz waveforms, the gain applied as it is */
	{ 0x47, 0x5a },
};
/*
 * ... and the analog ones, behind AW_TMCFG (the boost: skip mode, peak current 3.45 A, peak back, slew).
 * These too are Android's: what the phone's registers hold under Android (they differ from the chip's
 * own after a reset, so Android's driver set them); mainline Linux's aw86927.c sets the same values.
 */
static const uint8_t android_analog[][2] = {
	{ 0x70, 0x0f }, { 0x71, 0x93 }, { 0x72, 0x69 }, { 0x74, 0x89 }, { 0x75, 0x88 },
	{ AW_TRIM_LRA, 0x0d },  /* Android's f0 calibration of this motor (its log: "f0_cali_data = 0x0D") */
};

/* The analog registers opened or closed for writing (AW_TMCFG reads back as 0 either way: what tells is that the writes stick). */
static bool
unlock(bool open)
{
	uint8_t value = open ? 0x7d : 0x00;

	if (broken || !regs_write(AW_TMCFG, &value, 1)) {
		give_up("the analog registers could not be opened");
		return false;
	}
	say("haptic: %#04x <- %#04x (the analog registers %s)\n", AW_TMCFG, value, open ? "opened" : "closed");
	return true;
}

static bool
configure(void)
{
	say("haptic: Android's configuration is written\n");
	for (unsigned i = 0; i < sizeof(android_config) / sizeof(android_config[0]); i++) {
		if (!put(android_config[i][0], android_config[i][1])) {
			return false;
		}
	}
	if (!unlock(true)) {
		return false;
	}
	for (unsigned i = 0; i < sizeof(android_analog) / sizeof(android_analog[0]); i++) {
		if (!put(android_analog[i][0], android_analog[i][1])) {
			(void)unlock(false);
			return false;
		}
	}
	return unlock(false) && healthy("after the configuration");
}

static uint32_t states_seen;           /* the GLBRD5 states seen by the last wait, a bit each */

/* until the chip says it is in standby (GLBRD5 state 0), at most `ms' */
static bool
wait_standby(unsigned ms)
{
	states_seen = 0;
	for (unsigned waited = 0; waited <= ms; waited += 2) {
		uint8_t state = 0xff;

		if (!get(AW_GLBRD5, &state)) {
			return false;
		}
		states_seen |= 1u << (state & 0x0f);
		if ((state & 0x0f) == 0) {
			return true;
		}
		wait_ms(2);
	}
	return false;
}

/*
 * Android's waveforms into the chip's RAM: the file is a checksum (16 bits, of all that follows), the
 * address the RAM's contents begin at (0x0800), and those contents -- a version byte, the start and end
 * address of each waveform, the waveforms. Written through the data port with the address counting up,
 * then read back the same way and compared.
 */
static bool
load_waveforms(void)
{
	uint64_t size = 0;
	const uint8_t *file = hakios_wifi_file("aw8697_haptic.bin", &size);
	uint32_t sum = 0, base, length;
	uint8_t address[2], back[64];

	if (file == NULL || size < 8) {
		say("haptic: no aw8697_haptic.bin in fw/ of the small image (fw-haptic/ when it is built)\n");
		return false;
	}
	for (uint64_t i = 2; i < size; i++) {
		sum += file[i];
	}
	if ((sum & 0xffff) != (uint32_t)(file[0] << 8 | file[1])) {
		say("haptic: aw8697_haptic.bin does not add up (%#x, it says %#x): not loaded\n", sum & 0xffff, file[0] << 8 | file[1]);
		return false;
	}
	base = (uint32_t)(file[2] << 8 | file[3]);
	length = (uint32_t)size - 4;
	say("haptic: Android's waveforms: %u bytes from %#x on, %u waveforms\n", length, base, (unsigned)(((file[5] << 8 | file[6]) -
	    base - 1) / 4));
	if (!wait_standby(250) || !put(AW_SYSCTRL3, (uint8_t)(android_regs[AW_SYSCTRL3] | 0x04))) {
		give_up("the RAM could not be opened");
		return false;
	}
	address[0] = (uint8_t)(base >> 8);
	address[1] = (uint8_t)base;
	if (!put(AW_BASEADDRH, address[0]) || !put(AW_BASEADDRL, address[1]) || !regs_write(AW_RAMADDRH, address, 2)) {
		give_up("the RAM's address could not be set");
		return false;
	}
	for (uint32_t at = 0; at < length; at += 32) {
		unsigned n = length - at < 32 ? length - at : 32;

		if (!regs_write(AW_RAMDATA, file + 4 + at, n)) {
			give_up("the RAM could not be written");
			return false;
		}
	}
	if (!regs_write(AW_RAMADDRH, address, 2)) {
		give_up("the RAM's address could not be set again");
		return false;
	}
	for (uint32_t at = 0; at < length; at += sizeof(back)) {
		unsigned n = length - at < sizeof(back) ? length - at : sizeof(back);

		if (!regs_read(AW_RAMDATA, back, n) || memcmp(back, file + 4 + at, n) != 0) {
			say("haptic: the RAM reads back differently at %#x\n", base + at);
			give_up("the waveforms did not load");
			return false;
		}
	}
	if (!put(AW_SYSCTRL3, android_regs[AW_SYSCTRL3])) {
		return false;
	}
	say("haptic: Android's waveforms are in the chip's RAM and read back as written\n");
	return healthy("after the waveforms");
}

/* One waveform played once (Android's effect `effect' - 1), stopped at HAPTIC_MAX_MS at the latest. */
static bool
play_effect(unsigned effect)
{
	uint64_t began = mach_absolute_time(), ended;
	bool done;

	say("haptic: waveform %u is played once, at Android's gain %#x\n", effect, android_regs[AW_PLAYCFG2]);
	if (!wait_standby(250) || !put(AW_PLAYCFG3, 0x04) || !put(AW_WAVCFG1, (uint8_t)effect) || !put(AW_WAVCFG2, 0x00) ||
	    !put(AW_WAVCFG9, 0x00) || !put(AW_PLAYCFG2, android_regs[AW_PLAYCFG2]) || !healthy("before playing")) {
		return false;
	}
	{
		uint8_t go = 0x01;

		if (!regs_write(AW_PLAYCFG4, &go, 1)) {
			give_up("the start was not acknowledged");
			return false;
		}
	}
	wait_ms(2);
	done = wait_standby(HAPTIC_MAX_MS);
	absolutetime_to_nanoseconds(mach_absolute_time() - began, &ended);
	if (!done) {
		uint8_t stop = 0x02;

		say("haptic: STILL PLAYING after %u ms: stopped\n", HAPTIC_MAX_MS);
		(void)regs_write(AW_PLAYCFG4, &stop, 1);
		if (!wait_standby(50)) {
			give_up("it does not stop");
			return false;
		}
	}
	say("haptic: the waveform has played: back in standby after %llu ms (states seen meanwhile: %#x, a bit each; 0 is "
	    "standby)\n", ended / NSEC_PER_MSEC, states_seen);
	return healthy("after playing");
}

/* ---- haptic=5: the motor for iOS ------------------------------------------------------------------- */

#define ACTIVE_US               30000           /* a write that was not silence counts for this long */
#define SILENT_BELOW            256             /* of 32768: samples no larger are silence */
#define TAP_US                  60000           /* longer than this, and it is a vibration, not a tap */
#define SETTLE_US               8000            /* how long iOS's waveform is listened to before a tap: its loudness */
#define FOLLOW_US               20000           /* how often a vibration's strength follows iOS's */

#define TOLD_EVENTS             24              /* the first so many taps and vibrations are each said */
#define RENOIR_TAP_US           20000           /* renoir: how long a tap's pulse of direct play lasts */

static volatile uint64_t heard_us;              /* when the last write that was not silence came */
static volatile uint64_t quiet_us;              /* when the last write that was silence came */
static volatile uint64_t onset_us;              /* when iOS's last waveform began: the first write not silence after a pause */
static volatile uint32_t heard_peak;            /* the largest sample of that waveform, or since the service last looked */
static uint64_t written_frames;
static unsigned taps, loops, capped;            /* what has been played, for the run's end */

static uint64_t
now_us(void)
{
	uint64_t ns;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / NSEC_PER_USEC;
}

/* From the audio server's I/O thread: never waits. */
static int
haptic_write(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	int16_t samples[256];
	int error = 0;

	while (uio_resid(uio) >= 2) {
		user_ssize_t bytes = uio_resid(uio);
		uint32_t peak = 0;

		if (bytes > (user_ssize_t)sizeof(samples)) {
			bytes = sizeof(samples);
		}
		bytes &= ~(user_ssize_t)1;
		if ((error = uiomove((caddr_t)samples, (int)bytes, uio)) != 0) {
			break;
		}
		for (unsigned i = 0; i < bytes / 2; i++) {
			uint32_t v = samples[i] < 0 ? (uint32_t)-(int32_t)samples[i] : (uint32_t)samples[i];

			peak = v > peak ? v : peak;
		}
		written_frames += (uint64_t)bytes / 2;
		if (peak > SILENT_BELOW) {
			uint64_t now = now_us();

			if (heard_us == 0 || quiet_us > heard_us || now - heard_us >= ACTIVE_US) {
				onset_us = now;
				heard_peak = 0;
			}
			heard_us = now;
			if (peak > heard_peak) {
				heard_peak = peak;
			}
		} else {
			quiet_us = now_us();
		}
	}
	return error;
}

/*
 * A test's stand-in for iOS's waveform (hakios.mic.c, mic_tap=1: the microphone hears the motor): as if
 * a write of this loudness (of 32767) had just come, or silence for 0. The service plays it with all its
 * own limits, as it plays iOS's.
 */
void
hakios_haptic_pretend(uint32_t peak)
{
	uint64_t now = now_us();

	if (peak <= SILENT_BELOW) {
		quiet_us = now;
		return;
	}
	if (heard_us == 0 || quiet_us > heard_us || now - heard_us >= ACTIVE_US) {
		onset_us = now;
		heard_peak = 0;
	}
	heard_us = now;
	if (peak > heard_peak) {
		heard_peak = peak;
	}
}

static int
haptic_open(__unused dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	return 0;
}

static const struct cdevsw haptic_cdevsw = {
	.d_open = haptic_open, .d_close = haptic_open, .d_read = eno_rdwrt, .d_write = haptic_write,
	.d_ioctl = eno_ioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

/* Start a waveform without waiting for it: once, or (`forever') until stopped. */
static bool
start_waveform(unsigned waveform, bool forever, uint8_t gain)
{
	uint8_t go = 0x01;

	if (!put(AW_PLAYCFG3, 0x04) || !put(AW_WAVCFG1, (uint8_t)waveform) || !put(AW_WAVCFG2, 0x00) ||
	    !put(AW_WAVCFG9, forever ? 0xf0 : 0x00) || !put(AW_PLAYCFG2, gain)) {
		return false;
	}
	if (!regs_write(AW_PLAYCFG4, &go, 1)) {
		give_up("the start was not acknowledged");
		return false;
	}
	return true;
}

static bool
stop_waveform(void)
{
	uint8_t stop = 0x02;

	if (!regs_write(AW_PLAYCFG4, &stop, 1) || !wait_standby(50)) {
		give_up("it does not stop");
		return false;
	}
	return true;
}

/* the chip's state (GLBRD5: 0 standby, 7 and 11 seen while a waveform plays), 0xff if it cannot be read */
static unsigned
chip_state(void)
{
	uint8_t state = 0xff;

	return get(AW_GLBRD5, &state) ? state & 0x0f : 0xff;
}

/*
 * How strong to play what iOS plays. iOS drives its Taptic Engine with waveforms far under full
 * scale for most feedbacks (a tap's peak anywhere from 1000 to 33000 of 32768, a vibration's
 * about 17000), and followed in proportion (gain = Android's * peak / full scale) most of them came
 * out at a tenth of Android's gain or less: not felt at all. Android plays its own clicks and
 * vibrations at its gain, 0x80. So: Android's gain from a third of full scale up, and below that
 * down to five eighths of it for the softest -- never above Android's.
 */
/*
 * The motor's four actions for the service below: redwood's Awinic chip, or (board=renoir) the PMIC's
 * haptics, whose functions are further down (renoir_motor_*).
 */
static bool renoir_mode;
static bool renoir_motor_start(bool forever, uint8_t gain);
static bool renoir_motor_stop(void);
static bool renoir_motor_gain(uint8_t gain);
static bool renoir_motor_healthy(const char *when);
static bool renoir_motor_broken(void);

static bool
motor_start(unsigned waveform, bool forever, uint8_t gain)
{
	return renoir_mode ? renoir_motor_start(forever, gain) : start_waveform(waveform, forever, gain);
}

static bool
motor_stop(void)
{
	return renoir_mode ? renoir_motor_stop() : stop_waveform();
}

/* stopped already (the Awinic chip in standby; renoir's motor not playing) */
static bool
motor_idle(void)
{
	return renoir_mode ? renoir_motor_stop() : wait_standby(0);
}

static bool
motor_gain(uint8_t gain)
{
	return renoir_mode ? renoir_motor_gain(gain) : put(AW_PLAYCFG2, gain);
}

static bool
motor_healthy(const char *when)
{
	return renoir_mode ? renoir_motor_healthy(when) : healthy(when);
}

static unsigned
motor_state(void)
{
	return renoir_mode ? 0xfe : chip_state();
}

static uint8_t
gain_for(uint32_t peak)
{
	/* renoir: the PMIC's amplitude, 0..0xff, its full scale what Android's vibrations use */
	uint32_t top = renoir_mode ? 0xff : android_regs[AW_PLAYCFG2], low = top * 5 / 8, third = 32767 / 3;

	return (uint8_t)(peak >= third ? top : low + (top - low) * peak / third);
}

static void
serve(void)
{
	enum { IDLE, TAP, LOOP, REST } state = IDLE;
	uint64_t since = 0, rest_until = 0, last_look = 0, tap_onset = 0, followed = 0;
	uint8_t playing_gain = 0;
	unsigned follows = 0;                   /* how often the vibration playing now has changed strength */
	int major = cdevsw_add(-1, &haptic_cdevsw);

	if (major < 0 || devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "hakios-haptic") == NULL) {
		say("haptic: /dev/hakios-haptic cannot be made\n");
		return;
	}
	say("haptic: THE MOTOR PLAYS WHAT IOS PLAYS ON ITS ACTUATOR (/dev/hakios-haptic) from here on, until the run ends\n");
	while (!broken && !(renoir_mode && renoir_motor_broken())) {
		uint64_t now = now_us();
		bool active = heard_us != 0 && now - heard_us < ACTIVE_US;
		uint32_t peak = heard_peak;
		uint8_t gain = gain_for(peak);

		switch (state) {
		case IDLE:
		case TAP:
			/*
			 * A waveform of iOS's that has begun (after a pause: a tap of a series is a tap of its own) is
			 * a tap -- once its first milliseconds are heard: a waveform begins softly, and they say how
			 * loud it is; one that goes on without a pause, a vibration.
			 */
			if (active && onset_us != tap_onset && now - onset_us >= SETTLE_US) {
				uint64_t began = onset_us;

				if (state == TAP && !motor_idle() && !motor_stop()) {
					break;
				}
				if (!motor_start(1, false, gain ? gain : 1)) {
					break;
				}
				state = TAP;
				since = now;
				tap_onset = began;
				taps++;
				if (taps + loops <= TOLD_EVENTS) {
					wait_ms(4);
					say("haptic: tap %u: iOS's waveform peaks at %u of 32768: Android's waveform 1 at gain %#04x; the "
					    "chip's state %u\n", taps, peak, gain ? gain : 1, motor_state());
				}
				break;
			}
			if (state == IDLE) {
				break;
			}
			/* renoir: a tap is a short pulse of direct play, ended here (the Awinic chip ends its waveform 1 itself) */
			if (renoir_mode && now - since > RENOIR_TAP_US) {
				(void)motor_stop();
			}
			if (active && quiet_us < tap_onset && now - since > TAP_US) {
				if (motor_stop() && motor_start(11, true, gain ? gain : 1)) {
					state = LOOP;
					since = followed = now;
					playing_gain = gain ? gain : 1;
					follows = 0;
					loops++;
					heard_peak = 0;
					if (taps + loops <= TOLD_EVENTS) {
						wait_ms(4);
						say("haptic: vibration %u: iOS goes on playing (peak %u): Android's waveform 11 over and over at "
						    "gain %#04x, %u ms at most; the chip's state %u\n", loops, peak, gain ? gain : 1,
						    HAPTIC_MAX_MS, motor_state());
					}
				}
			} else if (!active && now - since > TAP_US) {
				state = motor_idle() || motor_stop() ? IDLE : state;
			}
			break;
		case LOOP:
			/* as strong as iOS's waveform is now (Android's gain at full scale, never more) */
			if (active && now - followed >= FOLLOW_US) {
				followed = now;
				heard_peak = 0;
				if (peak != 0 && (gain > playing_gain + 8 || gain + 8 < playing_gain)) {
					if (!motor_gain(gain)) {
						break;
					}
					playing_gain = gain;
					follows++;
				}
			}
			if (!active || now - since > HAPTIC_MAX_MS * 1000ull) {
				bool cut = now - since > HAPTIC_MAX_MS * 1000ull;

				if (cut) {
					capped++;
					rest_until = now + HAPTIC_MAX_MS * 1000ull;
				}
				if (motor_stop()) {
					state = rest_until > now ? REST : IDLE;
					if (taps + loops <= TOLD_EVENTS + 1) {
						say("haptic: vibration %u: %s after %llu ms (its strength changed %u times, last to gain %#04x); "
						    "the chip is in standby\n", loops, cut ? "CUT at the limit" : "iOS stopped, and so does the motor",
						    (now - since) / 1000, follows, playing_gain);
					}
				}
			}
			break;
		case REST:
			if (now >= rest_until) {
				state = IDLE;
			}
			break;
		}
		if (state != IDLE && state != REST && now - last_look > 50000) {
			last_look = now;
			if (!motor_healthy("while playing")) {
				break;
			}
		}
		wait_ms(4);
	}
	say("haptic: the motor is given up on (%u taps and %u vibrations played, %u cut at %u ms)\n", taps, loops, capped,
	    HAPTIC_MAX_MS);
}

/* ==================================================================================================== */
/*
 * renoir (board=renoir): not an Awinic chip on I2C but the PMIC's own haptics -- the PM8350B's
 * "hv-haptics" (SPMI slave 3: the configuration block at 0xf000, the pattern block at 0xf100, the
 * power block at 0xf200), Linux's drivers/input/misc/qcom-hv-haptics.c. renoir's device tree:
 * qcom,vmax-mv 2450, qcom,lra-period-us 4808 (~208 Hz), f0 0xc3..0xd2, brake on. See
 * devices/renoir/WIFI-BT-HAPTICS.md.
 *
 * Through the same PMIC arbiter as the torch (hakios.torch.c: its registers, renoir's are the same:
 * core 0xc440000, cnfg 0xc40a000, observer 0xe600000, channels 0xc600000), one transfer at a time
 * with the buttons' driver. Every write is to a register of a short list only, and read back; any
 * surprise stops the motor and all writing for the run.
 *
 *   haptic=1  reading only: the arbiter's channels for the three blocks (and whether they are ours
 *             to write), the blocks' registers as the phone left them
 *   haptic=4  ... and one pulse in "direct play": faults cleared, the module on, VMAX and the open-loop
 *             LRA period from the device tree, the amplitude haptic_amp (default 0x80 of 0xff), played
 *             haptic_ms (default 150, at most 500), stopped, and every register put back as found.
 *             Not done (yet): the high-voltage boost Linux turns on through the PMIC's PBS -- without it
 *             the pulse may be weak or none
 */
extern void hakios_spmi_lock(void);
extern void hakios_spmi_unlock(void);

#define R_ARB_CORE_PHYS         0x0c440000UL
#define R_ARB_CNFG_PHYS         0x0c40a000UL
#define R_ARB_OBSERVER_PHYS     0x0e600000UL
#define R_ARB_CHNLS_PHYS        0x0c600000UL
#define R_ARB_CHANNEL(n)        (0x900 + 4 * (n))
#define R_ARB_OWNER(n)          (0x700 + 4 * (n))
#define R_CMD_WRITE             (0u << 27)
#define R_CMD_READ              (1u << 27)

#define HV_CFG                  0x3f0           /* slave 3, peripheral 0xf0 */
#define HV_PTN                  0x3f1
#define HV_HPWR                 0x3f2
#define HV_FAULT_STATUS         0x0c
#define HV_INT_RT_STS           0x10
#define HV_EN_CTL               0x46            /* [7] the module on */
#define HV_VMAX                 0x48            /* mV / 50 */
#define HV_SPMI_PLAY            0x4c            /* [7] play, [2:0] the source: 1 = direct play */
#define HV_TLRA_OL_HIGH         0x5c            /* the open-loop LRA period / 5 us, [3:0] high */
#define HV_TLRA_OL_LOW          0x5d
#define HV_FAULT_CLR            0x66            /* [2] short circuit, [1] auto-resonance error, [0] power not ready */
#define HV_PTN_DIRECT_PLAY      0x26            /* the amplitude, 0..0xff */
#define HV_VMAX_MV              2450            /* renoir's qcom,vmax-mv */
#define HV_LRA_PERIOD_US        4808            /* renoir's qcom,lra-period-us */

struct hv_block {
	const char *name;
	uint16_t id, channel;
	bool found, ours;
};
static struct hv_block hv_cfg = { "the configuration block", HV_CFG }, hv_ptn = { "the pattern block", HV_PTN },
    hv_hpwr = { "the power block", HV_HPWR };
static vm_offset_t hv_core, hv_cnfg, hv_observer;
static bool hv_broken, hv_playing;

static const char *
hv_wait(vm_offset_t channel)
{
	uint64_t start = mach_absolute_time(), interval;
	uint32_t status;

	nanoseconds_to_absolutetime(2000 * (uint64_t)NSEC_PER_USEC, &interval);
	do {
		status = rd(channel + 0x08);
	} while (!(status & 1) && mach_absolute_time() - start < interval);
	return !(status & 1) ? "no answer from the arbiter" : (status & 4) ? "the arbiter refuses" : (status & 2) ?
	       "the transfer failed" : (status & 8) ? "dropped" : NULL;
}

static const char *
hv_read(const struct hv_block *b, uint8_t reg, uint8_t *bytes, uint32_t count)
{
	vm_offset_t channel = hv_observer + 0x80UL * b->channel;
	const char *problem = "no channel";
	uint32_t data[2] = { 0, 0 };

	for (int tries = 0; b->found && tries < 3; tries++) {
		if (!leaving) {
			hakios_spmi_lock();
		}
		wr(channel + 0x00, R_CMD_READ | (uint32_t)reg << 4 | (count - 1));
		problem = hv_wait(channel);
		data[0] = rd(channel + 0x18);
		data[1] = count > 4 ? rd(channel + 0x1c) : 0;
		if (!leaving) {
			hakios_spmi_unlock();
		}
		if (problem == NULL || strcmp(problem, "dropped") != 0) {
			break;
		}
	}
	for (uint32_t i = 0; problem == NULL && i < count; i++) {
		bytes[i] = (uint8_t)(data[i / 4] >> (8 * (i % 4)));
	}
	return problem;
}

static bool hv_put(struct hv_block *b, uint8_t reg, uint8_t value);
static vm_offset_t hakios_hv_channel_write(void);

/* the motor stopped, whatever else went wrong (no list, no read-back: stopping always goes) */
static void
hv_stop_now(const char *why)
{
	vm_offset_t channel = hakios_hv_channel_write();

	if (channel != 0) {
		if (!leaving) {
			hakios_spmi_lock();
		}
		wr(channel + 0x10, 0);
		wr(channel + 0x00, R_CMD_WRITE | (uint32_t)HV_SPMI_PLAY << 4);
		(void)hv_wait(channel);
		if (!leaving) {
			hakios_spmi_unlock();
		}
	}
	hv_playing = false;
	if (!hv_broken) {
		hv_broken = true;
		say("haptic: %s: the motor stopped, no more writing for the run\n", why);
	}
}

static bool
hv_put(struct hv_block *b, uint8_t reg, uint8_t value)
{
	static const struct { uint16_t id; uint8_t reg; } allowed[] = {
		{ HV_CFG, HV_EN_CTL }, { HV_CFG, HV_VMAX }, { HV_CFG, HV_SPMI_PLAY }, { HV_CFG, HV_TLRA_OL_HIGH },
		{ HV_CFG, HV_TLRA_OL_LOW }, { HV_CFG, HV_FAULT_CLR }, { HV_PTN, HV_PTN_DIRECT_PLAY },
	};
	vm_offset_t channel;
	const char *problem = NULL;
	uint8_t back = 0;
	bool ok = false;

	for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
		ok |= allowed[i].id == b->id && allowed[i].reg == reg;
	}
	if (hv_broken || !ok || !b->found || !b->ours ||
	    ((rd(hv_core + R_ARB_CHANNEL(b->channel)) >> 8) & 0xfff) != b->id) {
		hv_stop_now(ok ? "a write that may not be made" : "a register that is not on the list");
		return false;
	}
	channel = (vm_offset_t)ml_io_map(R_ARB_CHNLS_PHYS + 0x10000UL * b->channel, 0x1000);
	for (int tries = 0; tries < 3; tries++) {
		if (!leaving) {
			hakios_spmi_lock();
		}
		wr(channel + 0x10, value);
		wr(channel + 0x00, R_CMD_WRITE | (uint32_t)reg << 4);
		problem = hv_wait(channel);
		if (!leaving) {
			hakios_spmi_unlock();
		}
		if (problem == NULL || strcmp(problem, "dropped") != 0) {
			break;
		}
	}
	/* (the fault clear and the play register clear themselves or change as they work: not compared) */
	if (problem == NULL && reg != HV_FAULT_CLR && reg != HV_SPMI_PLAY) {
		problem = hv_read(b, reg, &back, 1);
		if (problem == NULL && back != value) {
			say("haptic: %s %#04x <- %#04x, but it reads %#04x\n", b->name, reg, value, back);
			problem = "a register that did not read back as written";
		}
	}
	if (problem != NULL) {
		say("haptic: %s %#04x <- %#04x: %s\n", b->name, reg, value, problem);
		hv_stop_now(problem);
		return false;
	}
	return true;
}

/* the configuration block's write channel, for hv_stop_now() (0 if there is none of ours) */
static vm_offset_t
hakios_hv_channel_write(void)
{
	static vm_offset_t mapped;

	if (mapped == 0 && hv_cfg.found && hv_cfg.ours) {
		mapped = (vm_offset_t)ml_io_map(R_ARB_CHNLS_PHYS + 0x10000UL * hv_cfg.channel, 0x1000);
	}
	return mapped;
}

static void
hv_find(struct hv_block *b)
{
	for (uint32_t n = 0; n < 512; n++) {
		uint32_t map = rd(hv_core + R_ARB_CHANNEL(n)), owner;

		if (map == 0 || ((map >> 8) & 0xfff) != b->id) {
			continue;
		}
		owner = rd(hv_cnfg + R_ARB_OWNER(n)) & 7;
		say("haptic: channel %u is %s (%#x); written by system %u%s\n", n, b->name, map, owner,
		    owner == 0 ? ", which is us" : "");
		if (!b->found || owner == 0) {
			b->found = true;
			b->channel = (uint16_t)n;
			b->ours = owner == 0;
		}
	}
	if (!b->found) {
		say("haptic: no channel of the arbiter is %s (%#x)\n", b->name, b->id);
	}
}

static void
hv_show(const struct hv_block *b)
{
	uint8_t r[8];
	char line[200];

	for (unsigned base = 0; base < 0x100; base += 0x20) {
		int n = snprintf(line, sizeof(line), "haptic: %s %#04x:", b->name, base);

		for (unsigned i = 0; i < 0x20; i += 8) {
			if (hv_read(b, (uint8_t)(base + i), r, 8) != NULL) {
				n += snprintf(line + n, sizeof(line) - n, " --");
				continue;
			}
			for (unsigned j = 0; j < 8; j++) {
				n += snprintf(line + n, sizeof(line) - n, " %02x", r[j]);
			}
		}
		say("%s\n", line);
	}
}

/* ---- renoir: the motor for iOS (haptic=5), the four actions serve() asks for ---------------------------- */

static bool
renoir_motor_start(bool forever, uint8_t gain)
{
	(void)forever;                          /* direct play goes on until stopped: serve() ends taps itself */
	if (hv_broken || !hv_put(&hv_ptn, HV_PTN_DIRECT_PLAY, gain)) {
		return false;
	}
	hv_playing = true;
	if (!hv_put(&hv_cfg, HV_SPMI_PLAY, 0x81)) {
		hv_stop_now("the start was not taken");
		return false;
	}
	return true;
}

static bool
renoir_motor_stop(void)
{
	if (!hv_playing) {
		return true;
	}
	if (hv_broken || !hv_put(&hv_cfg, HV_SPMI_PLAY, 0x01)) {
		hv_stop_now("the stop was not taken");
		return false;
	}
	hv_playing = false;
	return true;
}

static bool
renoir_motor_gain(uint8_t gain)
{
	return !hv_broken && hv_put(&hv_ptn, HV_PTN_DIRECT_PLAY, gain);
}

static bool
renoir_motor_healthy(const char *when)
{
	uint8_t fault = 0;

	if (hv_read(&hv_cfg, HV_FAULT_STATUS, &fault, 1) != NULL || (fault & 0x07) != 0) {
		say("haptic: %s: faults %#04x\n", when, fault);
		hv_stop_now("a fault");
		return false;
	}
	return true;
}

static bool
renoir_motor_broken(void)
{
	return hv_broken;
}

static void
renoir_haptics(void)
{
	uint8_t en = 0, vmax = 0, tlra[2] = { 0, 0 }, fault = 0, amp_was = 0;
	uint32_t ms = 150, amp = 0x80, period = HV_LRA_PERIOD_US / 5;

	hv_core = ml_io_map(R_ARB_CORE_PHYS, 0x1100);
	hv_cnfg = ml_io_map(R_ARB_CNFG_PHYS, 0x1000);
	hv_observer = ml_io_map(R_ARB_OBSERVER_PHYS, 0x10000);
	say("haptic: renoir: the PM8350B's haptics; arbiter version %#x; level %u (%s)\n", rd(hv_core + 0x000), level,
	    level >= 5 ? "the motor for iOS" : level >= 4 ? "one pulse" : "reading only");
	hv_find(&hv_cfg);
	hv_find(&hv_ptn);
	hv_find(&hv_hpwr);
	hv_show(&hv_cfg);
	hv_show(&hv_ptn);
	hv_show(&hv_hpwr);
	if (level < 4) {
		return;
	}
	if (!hv_cfg.ours || !hv_ptn.ours) {
		say("haptic: the blocks are not ours to write: nothing played\n");
		return;
	}
	PE_parse_boot_argn("haptic_ms", &ms, sizeof(ms));
	PE_parse_boot_argn("haptic_amp", &amp, sizeof(amp));
	ms = ms > 500 ? 500 : ms;
	amp = amp > 0xff ? 0xff : amp;
	if (hv_read(&hv_cfg, HV_EN_CTL, &en, 1) != NULL || hv_read(&hv_cfg, HV_VMAX, &vmax, 1) != NULL ||
	    hv_read(&hv_cfg, HV_TLRA_OL_HIGH, tlra, 2) != NULL || hv_read(&hv_cfg, HV_FAULT_STATUS, &fault, 1) != NULL ||
	    hv_read(&hv_ptn, HV_PTN_DIRECT_PLAY, &amp_was, 1) != NULL) {
		say("haptic: the registers cannot be read: nothing played\n");
		return;
	}
	say("haptic: found: enable %#04x, vmax %#04x (%u mV), open-loop period %#04x %#04x (%u us), faults %#04x, "
	    "amplitude %#04x\n", en, vmax, vmax * 50, tlra[0], tlra[1], ((tlra[0] & 0xf) << 8 | tlra[1]) * 5, fault, amp_was);
	if (level >= 5) {
		/* configured as for the pulse, then the service: what iOS plays on its actuator, until the run ends */
		if (hv_put(&hv_cfg, HV_FAULT_CLR, 0x07) && hv_put(&hv_cfg, HV_VMAX, HV_VMAX_MV / 50) &&
		    hv_put(&hv_cfg, HV_TLRA_OL_HIGH, (uint8_t)(period >> 8 & 0xf)) &&
		    hv_put(&hv_cfg, HV_TLRA_OL_LOW, (uint8_t)(period & 0xff)) && hv_put(&hv_cfg, HV_EN_CTL, 0x80)) {
			renoir_mode = true;
			serve();
		}
		if (hv_playing) {
			hv_stop_now("still playing at the end");
		}
		if (!hv_broken && hv_put(&hv_ptn, HV_PTN_DIRECT_PLAY, amp_was) && hv_put(&hv_cfg, HV_EN_CTL, en) &&
		    hv_put(&hv_cfg, HV_TLRA_OL_HIGH, tlra[0]) && hv_put(&hv_cfg, HV_TLRA_OL_LOW, tlra[1]) &&
		    hv_put(&hv_cfg, HV_VMAX, vmax)) {
			say("haptic: the registers are back as found\n");
		}
		return;
	}
	if (hv_put(&hv_cfg, HV_FAULT_CLR, 0x07) && hv_put(&hv_cfg, HV_VMAX, HV_VMAX_MV / 50) &&
	    hv_put(&hv_cfg, HV_TLRA_OL_HIGH, (uint8_t)(period >> 8 & 0xf)) &&
	    hv_put(&hv_cfg, HV_TLRA_OL_LOW, (uint8_t)(period & 0xff)) && hv_put(&hv_cfg, HV_EN_CTL, 0x80) &&
	    hv_put(&hv_ptn, HV_PTN_DIRECT_PLAY, (uint8_t)amp)) {
		say("haptic: playing: direct play, amplitude %#x, %u mV, %u us, for %u ms\n", amp, HV_VMAX_MV, period * 5, ms);
		hv_playing = true;
		if (hv_put(&hv_cfg, HV_SPMI_PLAY, 0x81)) {
			wait_ms(ms);
		}
		(void)hv_read(&hv_cfg, HV_FAULT_STATUS, &fault, 1);
		say("haptic: faults after playing: %#04x\n", fault);
		if (!hv_broken) {
			(void)hv_put(&hv_cfg, HV_SPMI_PLAY, 0x01);
			hv_playing = false;
		}
	}
	if (hv_playing) {
		hv_stop_now("still playing");
	}
	/* everything back as found (only if nothing went wrong: then the stop is all that is done) */
	if (!hv_broken && hv_put(&hv_ptn, HV_PTN_DIRECT_PLAY, amp_was) && hv_put(&hv_cfg, HV_EN_CTL, en) &&
	    hv_put(&hv_cfg, HV_TLRA_OL_HIGH, tlra[0]) && hv_put(&hv_cfg, HV_TLRA_OL_LOW, tlra[1]) &&
	    hv_put(&hv_cfg, HV_VMAX, vmax)) {
		say("haptic: the registers are back as found\n");
	}
}
/* ==================================================================================================== */

static void
haptic_thread(__unused void *parameter, __unused wait_result_t result)
{
	char board[16] = "";

	/* boot-arg board=renoir: the PMIC's haptics instead of redwood's Awinic chip */
	if (PE_parse_boot_argn("board", board, sizeof(board)) && strcmp(board, "renoir") == 0) {
		renoir_haptics();
		say("haptic: done\n");
		thread_terminate(current_thread());
		return;
	}
	tlmm = ml_io_map(TLMM_PHYS, TLMM_SIZE);
	say("haptic: level %u: %s\n", level, level == 1 ? "a look, reading only" : level == 2 ? "the reset released, registers read" :
	    level == 3 ? "the chip configured and its waveforms loaded, nothing played" : level == 4 ? "one effect played" :
	    "the motor for iOS");
	show_pins();
	if (level >= 2 && !((rd(GPIO_CFG(PIN_RESET)) & CFG_OUTPUT) && (rd(GPIO_IO(PIN_RESET)) & 2))) {
		/* (Android's driver: the pin low, 5 ms, high, and the chip ready for I2C 8 ms later) */
		say("haptic: the reset is released: gpio%u low for 5 ms, then high\n", PIN_RESET);
		reset_pin(false);
		wait_ms(5);
		reset_pin(true);
		wait_ms(8);
	}
	if (!chip_answers()) {
		say("haptic: (the reset pin %s)\n", (rd(GPIO_CFG(PIN_RESET)) & CFG_OUTPUT) && (rd(GPIO_IO(PIN_RESET)) & 2) ?
		    "drives high: the chip should answer" : "does not drive high: the chip is held in reset or not powered");
		if (released) {
			give_up("the chip does not answer");
		}
	} else {
		show_registers();
		if (level >= 3 && configure() && load_waveforms() && level == 4) {
			uint32_t effect = 1;

			PE_parse_boot_argn("haptic_effect", &effect, sizeof(effect));
			if (effect < 1 || effect > 10) {
				effect = 1;     /* (the short ones only) */
			}
			(void)play_effect(effect);
		} else if (level == 5 && !broken) {
			serve();
		}
	}
	say("haptic: done\n");
	thread_terminate(current_thread());
}

void
hakios_haptic_init(void)
{
	thread_t thread;

	if (!PE_parse_boot_argn("haptic", &level, sizeof(level)) || level == 0) {
		return;
	}
	if (level > 5) {
		level = 5;
	}
	if (kernel_thread_start(haptic_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}

/* At the run's end and on a panic's way out: the chip back into reset, if it was let out. */
void
hakios_haptic_quiesce(void)
{
	if (hv_playing) {                       /* renoir: a pulse that is still on is stopped */
		leaving = true;
		hv_stop_now("at the run's end");
	}
	if (renoir_mode) {
		say("haptic: at the run's end: %u taps and %u vibrations played (%u cut at %u ms), from %llu frames iOS wrote\n",
		    taps, loops, capped, HAPTIC_MAX_MS, written_frames);
	}
	if (released && tlmm != 0) {
		leaving = true;
		reset_pin(false);
		if (level == 5) {
			say("haptic: at the run's end: %u taps and %u vibrations played (%u cut at %u ms), from %llu frames iOS "
			    "wrote; the chip is held in reset\n", taps, loops, capped, HAPTIC_MAX_MS, written_frames);
		}
	}
}
