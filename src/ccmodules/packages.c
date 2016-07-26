/***
 Copyright © 2015 Intel Corporation

 Author: Auke-jan H. Kok <auke-jan.h.kok@intel.com>

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

#include <stdbool.h>
#include <stdio.h>

#include <glib.h>

#include "handlers.h"
#include "cloud_config.h"
#include "lib.h"

#define MOD "packages: "
#define COMMAND_SIZE 256

static gboolean packages_item(GNode* node, __unused__ gpointer data) {
	gchar command[COMMAND_SIZE];
	g_snprintf(command, COMMAND_SIZE,
#if defined(PACKAGE_MANAGER_SWUPD)
		"/usr/bin/swupd bundle-add %s",
#elif defined(PACKAGE_MANAGER_YUM)
		"/usr/bin/yum --assumeyes install %s",
#elif defined(PACKAGE_MANAGER_DNF)
		"/usr/bin/dnf install %s",
#elif defined(PACKAGE_MANAGER_APT)
		"/usr/bin/apt-get install %s",
#elif defined(PACKAGE_MANAGER_TDNF)
		"/usr/bin/tdnf --assumeyes install %s",
#endif
		(char*)node->data);
	LOG(MOD "Installing %s..\n", (char*)node->data);
	exec_task(command);
	return false;
}

void packages_handler(GNode *node) {
	LOG(MOD "Packages Handler running...\n");
	/*
	 * due to node possibly being a list of lists, just ignore all
	 * non-leave nodes.
	 */
	g_node_traverse(node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1, packages_item, NULL);
}

struct cc_module_handler_struct packages_cc_module = {
	.name = "packages",
	.handler = &packages_handler
};
