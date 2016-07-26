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

/*
 * List existing modules so they can be registered from main.c
 */

#pragma once

extern struct cc_module_handler_struct package_upgrade_cc_module;
extern struct cc_module_handler_struct write_files_cc_module;
extern struct cc_module_handler_struct packages_cc_module;
extern struct cc_module_handler_struct groups_cc_module;
extern struct cc_module_handler_struct users_cc_module;
extern struct cc_module_handler_struct ssh_authorized_keys_cc_module;
extern struct cc_module_handler_struct service_cc_module;
extern struct cc_module_handler_struct hostname_cc_module;
extern struct cc_module_handler_struct runcmd_cc_module;
extern struct cc_module_handler_struct envar_cc_module;
extern struct cc_module_handler_struct fbootcmd_cc_module;

struct cc_module_handler_struct *cc_module_structs[] =  {
	&package_upgrade_cc_module,
	&write_files_cc_module,
	&packages_cc_module,
	&groups_cc_module,
	&users_cc_module,
	&ssh_authorized_keys_cc_module,
	&service_cc_module,
	&hostname_cc_module,
	&runcmd_cc_module,
	&envar_cc_module,
	&fbootcmd_cc_module,
	NULL
};

