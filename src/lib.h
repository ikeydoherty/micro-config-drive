/***
 Copyright © 2015 Intel Corporation

 Author: Auke-jan H. Kok <auke-jan.h.kok@intel.com>
 Author: Julio Montes <julio.montes@intel.com>

 This file is part of micro-config-drive.

 micro-config-drive is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 micro-config-drive is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with micro-config-drive. If not, see <http://www.gnu.org/licenses/>.

 In addition, as a special exception, the copyright holders give
 permission to link the code of portions of this program with the
 OpenSSL library under certain conditions as described in each
 individual source file, and distribute linked combinations
 including the two.
 You must obey the GNU General Public License in all respects
 for all of the code used other than OpenSSL.  If you modify
 file(s) with this exception, you may extend this exception to your
 version of the file(s), but you are not obligated to do so.  If you
 do not wish to do so, delete this exception statement from your
 version.  If you delete this exception statement from all source
 files in the program, then also delete it here.
***/

#pragma once

#include <glib.h>

#include "debug.h"

#ifdef DEBUG
	#define STRINGIZE_DETAIL(x) #x
	#define STRINGIZE(x) STRINGIZE_DETAIL(x)
	#define LOGD(...) LOG(__BASE_FILE__":"STRINGIZE(__LINE__)" - "__VA_ARGS__)
#else
	#define LOGD(...)
	#define cloud_config_dump(...)
#endif /* DEBUG */

#define __unused__ __attribute__((unused))
#define __warn_unused_result__ __attribute__ ((warn_unused_result))

bool exec_task(const gchar* command_line);
void LOG(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int make_dir(const char* pathname, mode_t mode) __warn_unused_result__;
int chown_path(const char* pathname, const char* ownername, const char* groupname) __warn_unused_result__;
bool save_instance_id(const gchar* instance_id) __warn_unused_result__;
bool is_first_boot(void) __warn_unused_result__;
bool write_file(const char* data, gsize data_len, const gchar* file_path, int oflags, mode_t mode) __warn_unused_result__;
bool write_sudo_directives(const GString* data, const gchar* filename, int oflags) __warn_unused_result__;
bool write_ssh_keys(const GString* data, const gchar* username) __warn_unused_result__;
bool copy_file(const gchar* src, const gchar* dest) __warn_unused_result__;
bool mount_filesystem(const gchar* device, const gchar* mountdir, gchar** loop_device) __warn_unused_result__;
bool umount_filesystem(const gchar* mountdir, const gchar* loop_device) __warn_unused_result__;
bool gnode_free(GNode* node, gpointer data);
char* get_boot_id(void) __warn_unused_result__;
