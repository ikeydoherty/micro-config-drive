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

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <pwd.h>

#include <glib.h>

#include "handlers.h"
#include "disk.h"
#include "lib.h"
#include "userdata.h"
#include "datasources.h"
#include "default_user.h"
#include "openstack.h"
#include "async_task.h"


/* Long options */
enum {
	OPT_OPENSTACK_METADATA_FILE=1001,
	OPT_OPENSTACK_CONFIG_DRIVE,
	OPT_USER_DATA,
	OPT_USER_DATA_ONCE,
	OPT_METADATA,
	OPT_FIX_DISK,
	OPT_FIRST_BOOT_SETUP
};

/* supported datasources */
enum {
	DS_OPENSTACK=500,
};

static struct option opts[] = {
	{ "user-data-file",             required_argument, NULL, 'u' },
	{ "openstack-metadata-file",    required_argument, NULL, OPT_OPENSTACK_METADATA_FILE },
	{ "openstack-config-drive",     required_argument, NULL, OPT_OPENSTACK_CONFIG_DRIVE },
	{ "user-data",                  no_argument, NULL, OPT_USER_DATA },
	{ "user-data-once",             no_argument, NULL, OPT_USER_DATA_ONCE },
	{ "metadata",                   no_argument, NULL, OPT_METADATA },
	{ "help",                       no_argument, NULL, 'h' },
	{ "version",                    no_argument, NULL, 'v' },
	{ "fix-disk",                   no_argument, NULL, OPT_FIX_DISK },
	{ "first-boot-setup",           no_argument, NULL, OPT_FIRST_BOOT_SETUP},
	{ NULL, 0, NULL, 0 }
};

static int async_fixdisk(__unused__ gpointer null) {
	char* root_disk;

	root_disk = disk_by_path("/");
	if (!root_disk) {
		LOG("Root disk not found\n");
		return 1;
	}

	LOG("Checking disk %s\n", root_disk);
	if (!disk_fix(root_disk)) {
		free(root_disk);
		return 1;
	}

	free(root_disk);
	return 0;
}

static bool async_setup_first_boot(__unused__ gpointer null) {
	gchar command[LINE_MAX] = { 0 };
	GString* sudo_directives = NULL;

	/* default user will be able to use sudo */
	sudo_directives = g_string_new("");
	g_string_printf(sudo_directives, "# User rules for %s\n%s\n\n", DEFAULT_USER_USERNAME, DEFAULT_USER_SUDO);
	if (!write_sudo_directives(sudo_directives, DEFAULT_USER_USERNAME"-cloud-init", O_CREAT|O_TRUNC|O_WRONLY)) {
		LOG("Failed to enable sudo rule for user: %s\n", DEFAULT_USER_USERNAME);
	}
	g_string_free(sudo_directives, true);

	/* lock root account for security */
	g_snprintf(command, LINE_MAX, USERMOD_PATH " -p '!' root");
	return async_task_exec(command);
}

int main(int argc, char *argv[]) {
	int c;
	int i;
	unsigned int datasource = 0;
	int result_code = EXIT_SUCCESS;
	bool fix_disk = false;
	bool first_boot_setup = false;
	bool first_boot = false;
	char* userdata_filename = NULL;
	char* tmp_metadata_filename = NULL;
	char* tmp_data_filesystem = NULL;
	char metadata_filename[PATH_MAX] = { 0 };
	char data_filesystem_path[PATH_MAX] = { 0 };
	bool process_user_data = false;
	bool process_user_data_once = false;
	bool process_metadata = false;
	struct datasource_handler_struct *datasource_handler = NULL;
	gchar command[LINE_MAX] = { 0 };

	while (true) {
		c = getopt_long(argc, argv, "u:hv", opts, &i);

		if (c == -1) {
			break;
		}

		switch (c) {

		case 'u':
			userdata_filename = realpath(optarg, NULL);
			if (!userdata_filename) {
				LOG("Userdata file not found '%s'\n", optarg);
			}
			break;

		case 'h':
			LOG("Usage: %s [options]\n", argv[0]);
			LOG("-u, --user-data-file [file]            specify a custom user data file\n");
			LOG("    --openstack-metadata-file [file]   specify an Openstack metadata file\n");
			LOG("    --openstack-config-drive [path]    specify an Openstack config drive to process\n");
			LOG("                                       metadata and user data (iso9660 or vfat filesystem)\n");
			LOG("    --user-data                        get and process user data from data sources\n");
			LOG("    --user-data-once                   only on first boot get and process user data from data sources\n");
			LOG("    --metadata                         get and process metadata from data sources\n");
			LOG("-h, --help                             display this help message\n");
			LOG("-v, --version                          display the version number of this program\n");
			LOG("    --fix-disk                         fix disk and filesystem if it is needed\n");
			LOG("    --first-boot-setup                 setup the instance in its first boot\n");
			LOG("    --no-network                       %s will use local datasources to get data\n", argv[0]);
			exit(EXIT_SUCCESS);
			break;

		case 'v':
			fprintf(stdout, PACKAGE_NAME " " PACKAGE_VERSION "\n");
			exit(EXIT_SUCCESS);
			break;

		case '?':
			exit(EXIT_FAILURE);
			break;

		case OPT_OPENSTACK_METADATA_FILE:
			datasource = DS_OPENSTACK;
			tmp_metadata_filename = strdup(optarg);
			break;

		case OPT_OPENSTACK_CONFIG_DRIVE:
			datasource = DS_OPENSTACK;
			tmp_data_filesystem = strdup(optarg);
			break;

		case OPT_USER_DATA:
			process_user_data = true;
			break;

		case OPT_USER_DATA_ONCE:
			process_user_data_once = true;
			break;

		case OPT_METADATA:
			process_metadata = true;
			break;

		case OPT_FIX_DISK:
			fix_disk = true;
			break;

		case OPT_FIRST_BOOT_SETUP:
			first_boot_setup = true;
			break;

		}
	}

#ifdef HAVE_CONFIG_H
	LOG("micro-config-drive version: %s\n", PACKAGE_VERSION);
#endif /* HAVE_CONFIG_H */

	/* at one point in time this should likely be a fatal error */
	if (geteuid() != 0) {
		LOG("%s isn't running as root, this will most likely fail!\n", argv[0]);
	}

	if (make_dir(DATADIR_PATH, S_IRWXU) != 0) {
		LOG("Unable to create data dir '%s'\n", DATADIR_PATH);
	}

	if (!async_task_init()) {
		LOG("Unable to init async task\n");
	}

	/* process specific metadata file */
	if (tmp_metadata_filename) {
		if (realpath(tmp_metadata_filename, metadata_filename)) {
			switch (datasource) {
			case DS_OPENSTACK:
				if (!openstack_process_metadata_file(metadata_filename)) {
					result_code = EXIT_FAILURE;
				}
				break;
			default:
				LOG("Unsupported datasource '%d'\n", datasource);
			}
		} else {
			LOG("Metadata file not found '%s'\n", tmp_metadata_filename);
		}

		free(tmp_metadata_filename);
		tmp_metadata_filename = NULL;
	}

	/* process userdata/metadata using iso9660 or vfat filesystem */
	if (tmp_data_filesystem) {
		if (realpath(tmp_data_filesystem, data_filesystem_path)) {
			switch (datasource) {
			case DS_OPENSTACK:
				if (!openstack_process_config_drive(data_filesystem_path)) {
					result_code = EXIT_FAILURE;
				}
			break;
			default:
				LOG("Unsupported datasource '%d'\n", datasource);
			}
		} else {
			LOG("iso9660 or vfat filesystem not found '%s'\n", tmp_data_filesystem);
		}

		free(tmp_data_filesystem);
		tmp_data_filesystem = NULL;
	}

	if (process_user_data || process_metadata || process_user_data_once) {
		/* get/process userdata and metadata from datasources */
		for (i = 0; datasource_structs[i] != NULL; ++i) {
			if (datasource_structs[i]->init()) {
				datasource_handler = datasource_structs[i];
				if (!datasource_handler->start()) {
					result_code = EXIT_FAILURE;
				} else {
					first_boot = is_first_boot();
				}
				break;
			}
		}
	}

	if (fix_disk) {
		async_task_run((GThreadFunc)async_fixdisk, NULL);
	}

	if (first_boot_setup && first_boot) {
		async_task_run((GThreadFunc)async_setup_first_boot, NULL);
	}

	if (first_boot_setup && first_boot) {
		if (!getpwnam(DEFAULT_USER_USERNAME)) {
			/* default user will be used by ccmodules and datasources */
			g_snprintf(command, LINE_MAX, USERADD_PATH
					" -U -d '%s' -G '%s' -f '%s' -e '%s' -s '%s' -c '%s' -p '%s' '%s'"
					, DEFAULT_USER_HOME_DIR
					, DEFAULT_USER_GROUPS
					, DEFAULT_USER_INACTIVE
					, DEFAULT_USER_EXPIREDATE
					, DEFAULT_USER_SHELL
					, DEFAULT_USER_GECOS
					, DEFAULT_USER_PASSWORD
					, DEFAULT_USER_USERNAME);
			exec_task(command);
		} else {
			g_snprintf(command, LINE_MAX, USERMOD_PATH " -p '%s' %s", DEFAULT_USER_PASSWORD, DEFAULT_USER_USERNAME);
			exec_task(command);
		}
	}

	/* process metadata from metadata service */
	if (process_metadata && datasource_handler) {
		if (!datasource_handler->process_metadata()) {
			LOG("Process metadata failed\n");
			result_code = EXIT_FAILURE;
		}
	}

	/* process userdata file */
	if (userdata_filename) {
		if (!userdata_process_file(userdata_filename)) {
			result_code = EXIT_FAILURE;
		}

		free(userdata_filename);
		userdata_filename = NULL;
	}

	if (datasource_handler) {
		if (process_user_data || (process_user_data_once && first_boot)) {
			datasource_handler->process_userdata();
		}
	}

	async_task_finish();

	if (datasource_handler) {
		datasource_handler->finish();
	}

	exit(result_code);
}
