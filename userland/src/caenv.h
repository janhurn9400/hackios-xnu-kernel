/*
 * Boot-arg caenv=NAME=VALUE (as many as wanted): into the environment of the program a library of ours is in, for
 * CoreAnimation's own switches, which QuartzCore reads in every process that draws, not only in the window server
 * (inject.c does the same for backboardd). The frame rate ones, for instance (renoir, 90 Hz): an app's content
 * follows the iPhone X's 60 Hz unless QuartzCore is told otherwise in the app itself.
 *
 * Called from a library's constructor, before anything has asked QuartzCore for a frame rate.
 */
#ifndef HAKIOS_CAENV_H
#define HAKIOS_CAENV_H

#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

static inline unsigned
hakios_caenv(void)
{
	char arguments[1024], setting[128], *equals;
	size_t size = sizeof(arguments) - 1;
	unsigned count = 0;

	if (sysctlbyname("kern.bootargs", arguments, &size, NULL, 0) != 0) {
		return 0;
	}
	arguments[size] = 0;
	for (const char *argument = arguments; (argument = strstr(argument, "caenv=")) != NULL; argument += 6) {
		size_t length = strcspn(argument + 6, " ");

		if ((argument != arguments && argument[-1] != ' ') || length == 0 || length >= sizeof(setting)) {
			continue;
		}
		memcpy(setting, argument + 6, length);
		setting[length] = 0;
		if ((equals = strchr(setting, '=')) != NULL) {
			*equals = 0;
			setenv(setting, equals + 1, 1);
			count++;
		}
	}
	return count;
}

#endif
