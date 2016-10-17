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

#include "tzdev_plat.h"

/*
 * This functions are not implemented for ARTIK710.
 * But these are needed to keep compatibility on other platform.
 */

int plat_init(void)
{
	return 0;
}

int plat_preprocess(void)
{
	return 0;
}

int plat_postprocess(void)
{
	return 0;
}

int plat_dump_postprocess(char *uuid)
{
	return 0;
}
