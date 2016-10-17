/*********************************************************
 * Copyright (C) 2011 - 2016 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 *********************************************************/

#ifndef __TZLOG_PRINT_H__
#define __TZLOG_PRINT_H__

#include "log_level_ree.h"

extern kernel_log_level default_tzdev_local_log_level; /* OutPut */

void tzlog_print(kernel_log_level level, const char *fmt, ...);
void tzlog_print_for_tee(log_header_info header_info,
		kernel_log_level level,
		const char *label,
		const char *fmt, ...);

#endif /* __TZLOG_PRINT_H__ */
