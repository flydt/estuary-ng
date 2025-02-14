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
/*
 * A library to encapsulate functions and data structures to be reuse
 * by HSM copytool daemons for Lustre
 */
#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <sys/syscall.h>
#include <string.h>
#include <linux/lustre/lustre_fid.h>
#include <lustre/lustreapi.h>

#define ONE_MB 0x100000

#define MD5_ASCII 32 + 1

extern char cmd_name[PATH_MAX];
extern char fs_name[MAX_OBD_NAME + 1];
extern char path_prefix[ PATH_MAX ];

extern int err_major;

extern struct ct_options ct_opt;
extern const uint64_t MAX_OBJ_SIZE_LEVEL;
extern const uint64_t CHUNK_SIZE;
extern const uint64_t CHUNK_SIZE_STEP;
extern const uint64_t CHUNK_SIZE_MAX;
extern const int TIMEOUT_MS;
extern const double SLOW_IO_TIME;
extern const unsigned int MAX_HSM_REQUESTS;
extern int  max_requests;

/* Progress reporting period */
#define REPORT_INTERVAL_DEFAULT 30

struct ct_options {
    int o_copy_attrs;
    int o_daemonize;
    int o_dry_run;
    int o_abort_on_error;
    int o_verbose;
    int o_archive_cnt;
    int o_archive_id[LL_HSM_ORIGIN_MAX_ARCHIVE];
    int o_report_int;
    char *o_config;
    char *o_mnt;
    int o_mnt_fd;
};

struct ct_th_data {
    struct hsm_action_item *hai;
    long hal_flags;
};

/*
 * Basic struct to store a file's stripe size and stripe count
 */
typedef struct strippingInfo {
    __u32 lmm_stripe_size;
    __u16 lmm_stripe_count;
} strippingInfo;

/* hsm_copytool_private will hold an open FD on the lustre mount point
 * for us. This is to make sure it doesn't drop out from under us (and
 * remind the admin to shutdown the copytool before unmounting).
 */
extern struct hsm_copytool_private *ctdata;

/*
 * ct_archive, ct_restore, ct_remove & ct_cancel are declared here but must be
 * defined
 * by the implemented by the user of libct as they are specific to a
 * given archival platform.
 */
int ct_archive(const struct hsm_action_item *hai, const long hal_flags, char *file_name);
int ct_restore(const struct hsm_action_item *hai, const long hal_flags, char *path);
int ct_remove(const struct hsm_action_item *hai, const long hal_flags, char *object_name);
int ct_cancel(const struct hsm_action_item *hai, const long hal_flags);

int should_retry(int *retry_count);

/*
 * Return current time in sec since epoch
 */
double ct_now(void);

/*
 * For a given FD, find out the stripe size and stripe count and
 * return them in a pre-allocated strippingInfo struct
 */
int ct_save_stripe(int src_fd, const char *src, strippingInfo *params);

int ct_path_lustre(char *buf, int sz, const char *mnt, const lustre_fid *fid);
int ct_path_archive(char *buf, int sz, const lustre_fid *fid);
bool ct_is_retryable(int err);

/*
 * Notify the coordinator that an action was completed
 */
int ct_action_done(struct hsm_copyaction_private **phcp,
                   const struct hsm_action_item *hai, int hp_flags, int ct_rc);

/*
 * Notify the coordinator that an action is starting
 * ct_begin is only a wrapper around ct_begin_restore
 */
int ct_begin(struct hsm_copyaction_private **phcp,
             const struct hsm_action_item *hai);
int ct_begin_restore(struct hsm_copyaction_private **phcp,
                     const struct hsm_action_item *hai, int mdt_index,
                     int open_flags);

/*
 * Trigger cleanup when receiving signal
 */
void handler(int signal);

int ct_setup(void);

int ct_cleanup(void);

int ct_process_item(struct hsm_action_item *hai, const long hal_flags);

void *ct_thread(void *data);

int ct_process_item_async(const struct hsm_action_item *hai, long hal_flags);

/* Daemon waits for messages from the kernel; run it in the background. */
int ct_run(void);

int llapi_hsm_action_progress_ex(struct hsm_copyaction_private *hcp,
                                 const struct hsm_extent *he, __u64 total,
                                 int hp_flags);
int llapi_hsm_action_begin_ex(struct hsm_copyaction_private **phcp,
                              const struct hsm_copytool_private *ct,
                              const struct hsm_action_item *hai,
                              int restore_mdt_index, int restore_open_flags,
							  bool is_error);
