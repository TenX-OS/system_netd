/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <dlfcn.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define LOG_TAG "InterfaceController"
#include <cutils/log.h>
#include <netutils/ifc.h>
#include <private/android_filesystem_config.h>

#include "NetdConstants.h"

#include "InterfaceController.h"

char if_cmd_lib_file_name[] = "/system/lib/libnetcmdiface.so";
char set_cmd_func_name[] = "net_iface_send_command";
char set_cmd_init_func_name[] = "net_iface_send_command_init";
char set_cmd_fini_func_name[] = "net_iface_send_command_fini";

InterfaceController::InterfaceController()
	: sendCommand_(NULL) {
	libh_ = dlopen(if_cmd_lib_file_name, RTLD_NOW | RTLD_LOCAL);
	if (libh_ == NULL) {
		const char *err_str = dlerror();
		ALOGW("Warning (%s) while opening the net interface command library", err_str ? err_str : "unknown");
	} else {
		sendCommandInit_ = (int (*)(void))dlsym(libh_, set_cmd_init_func_name);
		if (sendCommandInit_ == NULL) {
			const char *err_str = dlerror();
			ALOGW("Error (%s) while searching for the interface command init function", err_str ? err_str : "unknown");
		} else if (sendCommandInit_()) {
			ALOGE("Can't init the interface command API");
			return;
		}
		sendCommandFini_ = (int (*)(void))dlsym(libh_, set_cmd_fini_func_name);
		if (sendCommandFini_ == NULL) {
			const char *err_str = dlerror();
			ALOGW("Error (%s) while searching for the interface command fini function", err_str ? err_str : "unknown");
		}
		sendCommand_ = (int (*)(int, char **, char **))dlsym(libh_, set_cmd_func_name);
		if (sendCommand_ == NULL) {
			const char *err_str = dlerror();
			ALOGE("Error (%s) while searching for the interface command function", err_str ? err_str : "unknown");
			return;
		}
	}
}

InterfaceController::~InterfaceController() {
	if (sendCommandFini_) {
		if (sendCommandFini_()) {
			ALOGE("Can't shutdown the interface command API");
		}
	}
	if (libh_) {
		int err = dlclose(libh_);
		if (err) {
			const char *err_str = dlerror();
			ALOGE("Error (%s) while closing the net interface command library", err_str ? err_str : "unknown");
		}
	}
}

/*
 * Arguments:
 *	  argv[2] - wlan interface
 *	  argv[3] - command
 *	  argv[4] - argument
 *	  rbuf	- returned buffer
 */
int InterfaceController::interfaceCommand(int argc, char *argv[], char **rbuf) {
	int ret = -ENOSYS;
	if (sendCommand_)
		ret = sendCommand_(argc, argv, rbuf);

	return ret;
}

int InterfaceController::writeIPv6ProcPath(const char *interface, const char *setting, const char *value) {
	char *path;
	asprintf(&path, "/proc/sys/net/ipv6/conf/%s/%s", interface, setting);
	int success = writeFile(path, value, strlen(value));
	free(path);
	return success;
}

int InterfaceController::setEnableIPv6(const char *interface, const int on) {
	// When IPv6 is on, accept RAs regardless of forwarding state.
	// When IPv6 is off, accept RAs only if forwarding is off (the default).
	const char *accept_ra = on ? "2" : "1";
	if (writeIPv6ProcPath(interface, "accept_ra", accept_ra)) {
		return -1;
	}

	// When disable_ipv6 changes from 1 to 0, the kernel starts autoconf.
	// When disable_ipv6 changes from 0 to 1, the kernel clears all autoconf
	// addresses and routes and disables IPv6 on the interface.
	const char *disable_ipv6 = on ? "0" : "1";
	return writeIPv6ProcPath(interface, "disable_ipv6", disable_ipv6);
}

int InterfaceController::setIPv6PrivacyExtensions(const char *interface, const int on) {
	// 0: disable IPv6 privacy addresses
	// 0: enable IPv6 privacy addresses and prefer them over non-privacy ones.
	return writeIPv6ProcPath(interface, "use_tempaddr", on ? "2" : "0");
}
