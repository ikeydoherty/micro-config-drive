/***
 Copyright © 2015 Intel Corporation

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

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include <glib.h>
#include <json-glib/json-glib.h>

#include "openstack.h"
#include "handlers.h"
#include "lib.h"
#include "userdata.h"
#include "json.h"
#include "default_user.h"
#include "disk.h"
#include "async_task.h"

#define MOD "openstack: "
#define OPENSTACK_METADATA_API "latest"
#define OPENSTACK_METADATA_FILE "/openstack/"OPENSTACK_METADATA_API"/meta_data.json"
#define OPENSTACK_USERDATA_FILE "/openstack/"OPENSTACK_METADATA_API"/user_data"
#define OPENSTACK_METADATA_ID_FILE DATADIR_PATH "/openstack_metadata_id"
#define OPENSTACK_USER_DATA_ID_FILE DATADIR_PATH "/openstack_user_data_id"

static bool openstack_process_config_drive_metadata(void);
static bool openstack_process_config_drive_userdata(void);
static void openstack_run_handler(GNode *node, __unused__ gpointer null);
static bool openstack_load_metadata_file(const gchar* filename);
static void openstack_process_uuid(GNode* node, __unused__ gpointer *data);

static int openstack_metadata_not_implemented(GNode* node);
static int openstack_metadata_keys(GNode* node);
static int openstack_metadata_hostname(GNode* node);
static int openstack_metadata_files(GNode* node);
static int openstack_metadata_uuid(GNode* node);

bool openstack_init(void);
bool openstack_start(void);
bool openstack_process_metadata(void);
bool openstack_process_userdata(void);
void openstack_finish(void);

enum {
	SOURCE_CONFIG_DRIVE = 101,
	SOURCE_NONE
};

static int data_source = SOURCE_NONE;

static char config_drive_disk[PATH_MAX] = { 0 };

static char *config_drive_loop_device = NULL;

static char config_drive_mount_path[] = DATADIR_PATH "/config-2-XXXXXX";

static char metadata_file[PATH_MAX] = { 0 };

static char userdata_file[PATH_MAX] = { 0 };

static GNode* metadata_node = NULL;

typedef int (*openstack_metadata_data_func)(GNode*);

struct openstack_metadata_data {
	const gchar* key;
	openstack_metadata_data_func func;
};

static struct openstack_metadata_data openstack_metadata_options[] = {
	{ "random_seed",        openstack_metadata_not_implemented },
	{ "uuid",               openstack_metadata_uuid            },
	{ "availability_zone",  openstack_metadata_not_implemented },
	{ "keys",               openstack_metadata_keys            },
	{ "hostname",           openstack_metadata_hostname        },
	{ "launch_index",       openstack_metadata_not_implemented },
	{ "public_keys",        openstack_metadata_not_implemented },
	{ "project_id",         openstack_metadata_not_implemented },
	{ "name",               openstack_metadata_not_implemented },
	{ "files",              openstack_metadata_files           },
	{ "meta",               openstack_metadata_not_implemented },
	{ NULL }
};

static GHashTable* openstack_metadata_options_htable = NULL;

struct datasource_handler_struct openstack_datasource = {
	.datasource="openstack",
	.init=openstack_init,
	.start=openstack_start,
	.process_metadata=openstack_process_metadata,
	.process_userdata=openstack_process_userdata,
	.finish=openstack_finish
};

bool openstack_init() {
	gchar* device = NULL;

	data_source = SOURCE_NONE;

	if (disk_by_label("config-2", &device)) {
		data_source = SOURCE_CONFIG_DRIVE;
		g_strlcpy(config_drive_disk, device, PATH_MAX);
		g_free(device);
		return true;
	}

	LOG(MOD "config drive was not found\n");
	return false;
}

bool openstack_start(void) {
	size_t i = 0;

	switch(data_source) {
	case SOURCE_CONFIG_DRIVE:
		/* create mount directory */
		if (!mkdtemp(config_drive_mount_path)) {
			LOG(MOD "Unable to create directory '%s'\n", config_drive_mount_path);
			return false;
		}

		/* mount config-2 disk */
		if (!mount_filesystem(config_drive_disk, config_drive_mount_path, &config_drive_loop_device)) {
			LOG(MOD "Unable to mount config drive '%s'\n", config_drive_disk);
			rmdir(config_drive_mount_path);
			return false;
		}

		g_snprintf(metadata_file, PATH_MAX, "%s%s", config_drive_mount_path, OPENSTACK_METADATA_FILE);
		break;

	default:
		LOG(MOD "Datasource not found\n");
		return false;
	}

	/* instance-id must be the first metadata key to process */
	if (!openstack_load_metadata_file(metadata_file)) {
		LOG(MOD "Load metadata file '%s' failed\n", metadata_file);
		return false;
	}

	openstack_metadata_options_htable = g_hash_table_new(g_str_hash, g_str_equal);
	for (i = 0; openstack_metadata_options[i].key != NULL; ++i) {
		g_hash_table_insert(openstack_metadata_options_htable, (gpointer)openstack_metadata_options[i].key,
		                    *((openstack_metadata_data_func**)(&openstack_metadata_options[i].func)));
	}

	g_node_children_foreach(metadata_node, G_TRAVERSE_ALL, (GNodeForeachFunc)openstack_process_uuid, NULL);

	return true;
}

bool openstack_process_metadata(void) {
	static bool cache_metadata_id = false;
	struct stat st;
	char* metadata_id = NULL;
	char* boot_id = NULL;

	if (cache_metadata_id) {
		LOG(MOD "metadata was already processed\n");
		return true;
	}

	if (stat(OPENSTACK_METADATA_ID_FILE, &st) == 0) {
		if (!g_file_get_contents(OPENSTACK_METADATA_ID_FILE, &metadata_id, NULL, NULL)) {
			LOG(MOD "Unable to read file '%s'\n", OPENSTACK_METADATA_ID_FILE);
		}
		if(metadata_id) {
			boot_id = get_boot_id();
			if (g_strcmp0(boot_id, metadata_id) == 0) {
				cache_metadata_id = true;
				g_free(boot_id);
				g_free(metadata_id);
				LOG(MOD "metadata was already processed\n");
				return true;
			}
			g_free(boot_id);
			g_free(metadata_id);
		}
	}

	switch(data_source) {
	case SOURCE_CONFIG_DRIVE:
		if (!openstack_process_config_drive_metadata()) {
			LOG(MOD "Process config drive metadata failed\n");
			return false;
		}
		break;

	default:
		LOG(MOD "Datasource not found\n");
		return false;
	}

	cache_metadata_id = true;
	boot_id = get_boot_id();
	if (!write_file(boot_id, strlen(boot_id), OPENSTACK_METADATA_ID_FILE, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR)) {
		LOG("Unable to save metadata id in '%s'\n", OPENSTACK_METADATA_ID_FILE);
	}
	g_free(boot_id);

	return true;
}

bool openstack_process_userdata(void) {
	static bool cache_user_data_id = false;
	struct stat st;
	char* user_data_id = NULL;
	char* boot_id = NULL;

	if (cache_user_data_id) {
		LOG(MOD "user data was already processed\n");
		return true;
	}

	if (stat(OPENSTACK_USER_DATA_ID_FILE, &st) == 0) {
		if (!g_file_get_contents(OPENSTACK_USER_DATA_ID_FILE, &user_data_id, NULL, NULL)) {
			LOG(MOD "Unable to read file '%s'\n", OPENSTACK_USER_DATA_ID_FILE);
		}
		if(user_data_id) {
			boot_id = get_boot_id();
			if (g_strcmp0(boot_id, user_data_id) == 0) {
				cache_user_data_id = true;
				g_free(boot_id);
				g_free(user_data_id);
				LOG(MOD "user data was already processed\n");
				return true;
			}
			g_free(boot_id);
			g_free(user_data_id);
		}
	}

	switch(data_source) {
	case SOURCE_CONFIG_DRIVE:
		if (!openstack_process_config_drive_userdata()) {
			LOG(MOD "Process config drive user data failed\n");
		}
		break;

	default:
		LOG(MOD "Datasource not found\n");
		return false;
	}

	cache_user_data_id = true;
	boot_id = get_boot_id();
	if (!write_file(boot_id, strlen(boot_id), OPENSTACK_USER_DATA_ID_FILE, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR)) {
		LOG("Unable to save user data id in '%s'\n", OPENSTACK_USER_DATA_ID_FILE);
	}
	g_free(boot_id);

	return true;
}

void openstack_finish(void) {
	switch(data_source) {
	case SOURCE_CONFIG_DRIVE:
		if (!umount_filesystem(config_drive_mount_path, config_drive_loop_device)) {
			LOG(MOD "umount filesystem failed '%s'\n", config_drive_mount_path);
		}
		if (config_drive_loop_device) {
			g_free(config_drive_loop_device);
			config_drive_loop_device = NULL;
		}
		break;
	}

	if (userdata_file[0]) {
		remove(userdata_file);
		userdata_file[0] = 0;
	}

	if (metadata_node) {
		g_node_traverse(metadata_node, G_POST_ORDER, G_TRAVERSE_ALL, -1, (GNodeTraverseFunc)gnode_free, NULL);
		g_node_destroy(metadata_node);
		metadata_node = NULL;
	}

	if (openstack_metadata_options_htable) {
		g_hash_table_destroy(openstack_metadata_options_htable);
		openstack_metadata_options_htable = NULL;
	}
}

gboolean openstack_process_config_drive(const gchar* path) {
	data_source = SOURCE_CONFIG_DRIVE;
	gchar* device = NULL;

	g_strlcpy(config_drive_disk, path, PATH_MAX);

	if (!config_drive_disk[0] && disk_by_label("config-2", &device)) {
		g_strlcpy(config_drive_disk, device, PATH_MAX);
		g_free(device);
	}

	if (!openstack_start()) {
		return false;
	}

	if (!openstack_process_config_drive_metadata()) {
		LOG(MOD "process config drive metadata failed\n");
		return false;
	}

	if (!openstack_process_config_drive_userdata()) {
		LOG(MOD "process config drive userdata failed\n");
	}

	openstack_finish();

	return true;
}

gboolean openstack_process_metadata_file(const gchar* filename) {
	if (!metadata_node && !openstack_load_metadata_file(filename)) {
		LOG(MOD "Load metadata file '%s'failed\n", filename);
		return false;
	}

	g_node_children_foreach(metadata_node, G_TRAVERSE_ALL, (GNodeForeachFunc)openstack_run_handler, NULL);

	return true;
}

static bool openstack_load_metadata_file(const gchar* filename) {
	GError* error = NULL;
	JsonParser* parser = NULL;
	gboolean result = false;

	parser = json_parser_new();
	json_parser_load_from_file(parser, filename, &error);
	if (error) {
		LOG(MOD "Unable to parse '%s': %s\n", filename, error->message);
		g_error_free(error);
		goto fail1;
	}

	metadata_node = g_node_new(g_strdup(filename));
	json_parse(json_parser_get_root(parser), metadata_node, false);
	cloud_config_dump(metadata_node);

	result = true;

fail1:
	g_object_unref(parser);
	return result;
}

static bool openstack_process_config_drive_metadata(void) {
	if (!openstack_process_metadata_file(metadata_file)) {
		LOG(MOD "Using config drive get and process metadata failed\n");
		return false;
	}

	return true;
}

static bool openstack_process_config_drive_userdata(void) {
	int fd_tmp = 0;
	struct stat st;
	gchar userdata_drive_path[PATH_MAX] = { 0 };

	g_snprintf(userdata_drive_path, PATH_MAX, "%s%s", config_drive_mount_path, OPENSTACK_USERDATA_FILE);

	if (stat(userdata_drive_path, &st) != 0) {
		LOG(MOD "User data file not found in config drive\n");
		return false;
	}

	g_strlcpy(userdata_file, DATADIR_PATH "/userdata-XXXXXX", PATH_MAX);

	fd_tmp = mkstemp(userdata_file);
	if (-1 == fd_tmp) {
		LOG(MOD "Unable to create a temporal file\n");
		return false;
	}
	if (close(fd_tmp) == -1) {
		LOG(MOD "Close file '%s' failed\n", userdata_file);
		return false;
	}

	if (!copy_file(userdata_drive_path, userdata_file)) {
		LOG(MOD "Copy file '%s' failed\n", userdata_drive_path);
		return false;
	}

	if (!userdata_process_file(userdata_file)) {
		LOG(MOD "Unable to process userdata\n");
		return false;
	}

	return true;
}

static void openstack_run_handler(GNode *node, __unused__ gpointer null) {
	if (node->data) {
		gpointer ptr = g_hash_table_lookup(openstack_metadata_options_htable, node->data);
		if(ptr) {
			LOG(MOD "Metadata using '%s' handler\n", (char*)node->data);
			openstack_metadata_data_func func = *(openstack_metadata_data_func*)(&ptr);
			async_task_run((GThreadFunc)func, node->children);
			return;
		}
		LOG(MOD "Metadata no handler for '%s'\n", (char*)node->data);
	}
}

static void openstack_process_uuid(GNode* node, __unused__ gpointer *data) {
	if (node->data && g_strcmp0(node->data, "uuid") == 0) {
		openstack_metadata_uuid(node->children);
		g_node_unlink(node);
		g_node_destroy(node);
	}
}

static int openstack_metadata_not_implemented(GNode* node) {
	LOG(MOD "Metadata '%s' not implemented yet\n", (char*)node->parent->data);
	return 0;
}

static int openstack_metadata_keys(GNode* node) {
	GString* ssh_key;
	while (node) {
		if (g_strcmp0("data", node->data) == 0) {
			LOG(MOD "keys processing %s\n", (char*)node->data);
			ssh_key = g_string_new(node->children->data);
			if (!write_ssh_keys(ssh_key, DEFAULT_USER_USERNAME)) {
				LOG(MOD "Cannot Write ssh key\n");
			}
			g_string_free(ssh_key, true);
		} else {
			LOG(MOD "keys nothing to do with %s\n", (char*)node->data);
		}
		node = node->next;
	}
	return 0;
}

static int openstack_metadata_hostname(GNode* node) {
	gchar command[LINE_MAX];
	if (is_first_boot()) {
		g_snprintf(command, LINE_MAX, HOSTNAMECTL_PATH " set-hostname '%s'", (char*)node->data);
		return exec_task(command);
	}
	return 0;
}

static int openstack_metadata_files(GNode* node) {
	gchar content_path[LINE_MAX] = { 0 };
	gchar path[LINE_MAX] = { 0 };
	gchar src_content_file[PATH_MAX] = { 0 };
	while (node) {
		if (g_strcmp0("content_path", node->data) == 0) {
			if (node->children) {
				g_strlcpy(content_path, node->children->data, LINE_MAX);
			}
		} else if (g_strcmp0("path", node->data) == 0) {
			if (node->children) {
				g_strlcpy(path, node->children->data, LINE_MAX);
			}
		} else {
			content_path[0] = 0;
			path[0] = 0;
			LOG(MOD "files nothing to do with %s\n", (char*)node->data);
		}

		if (content_path[0] && path[0]) {
			switch (data_source) {
			case SOURCE_CONFIG_DRIVE:
				g_snprintf(src_content_file, PATH_MAX, "%s/openstack/%s",
				           config_drive_mount_path, content_path);
				if (!copy_file(src_content_file, path)) {
					LOG(MOD "Copy file '%s' failed\n", src_content_file);
				}
			break;

			case SOURCE_NONE:
				if (!copy_file(content_path, path)) {
					LOG(MOD "Copy file '%s' failed\n", content_path);
				}
			break;
			}
		}

		node = node->next;
	}
	return 0;
}

static int openstack_metadata_uuid(GNode* node) {
	LOG(MOD "Saving instance id '%s'\n", (char*)node->data);
	if (!save_instance_id(node->data)) {
		LOG(MOD "Save instance id failed\n");
	}
	return 0;
}
