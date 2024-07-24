/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.htm
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2015, 2016, Universite Laval
 * Authors: Simon Guilbault, Frederick Lefebvre
 *
 *
 * Part of this file include code from file lhsmtool_posix.c (licensed under
 * a GPLv2 license) that can be found in Lustre's git repository here :
 * git://git.hpdd.intel.com/fs/lustre-release.git
 */
/*
 * Copyright (c) 2020, Irish Centre for High End Computing (ICHEC), NUI Galway
 * Authors:
 *     Ciarán O'Rourke <ciaran.orourke@ichec.ie>,
 *     Sophie Wenzel-Teuber <sophie.wenzel-teuber@ichec.ie>
 *
 * Update to a new version of Lustre and Libs3
 */
/* HSM copytool program for S3 object storage.
 *
 * An HSM copytool daemon acts on action requests from Lustre to copy files
 * to and from an HSM archive system.
 *
 */
#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <string.h>
#include <linux/lustre/lustre_fid.h>
#include <lustre/lustreapi.h>
#include <libs3.h>

#include "growbuffer.h"
#include "hsm_s3_utils.h"

#define RETRYCOUNT 5

#define MD5_ASCII 32 + 1

extern char access_key[ S3_MAX_KEY_SIZE ];
extern char secret_key[ S3_MAX_KEY_SIZE ];
extern char host[ S3_MAX_HOSTNAME_SIZE ];
extern char bucket_name[ S3_MAX_BUCKET_NAME_SIZE ];

static int ct_parseopts(int argc, char *const *argv);

static int ct_archive_data(struct hsm_copyaction_private *hcp, const char *src,
			   const char *dst, int src_fd, struct stat *src_st,
			   const struct hsm_action_item *hai, long hal_flags);

static int ct_archive_data_big(struct hsm_copyaction_private *hcp,
			       const char *src, const char *dst, int src_fd,
			       struct stat *src_st,
			       const struct hsm_action_item *hai,
			       long hal_flags);

static int ct_restore_data(struct hsm_copyaction_private *hcp, const char *src,
			   const char *dst, int dst_fd,
			   const struct hsm_action_item *hai, long hal_flags,
			   char *path);

static int ct_s3_cleanup(void);
