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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "ct_common.h"
#include "tlog.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <sys/xattr.h>
#include <sys/time.h>
#include <linux/lustre/lustre_fid.h>
#include <lustre/lustreapi.h>

char cmd_name[PATH_MAX];
char fs_name[MAX_OBD_NAME + 1];

// file size bigger than 200M will use multipart upload
// multipart upload use chunk size of 50MB
const uint64_t MAX_OBJ_SIZE_LEVEL = 200 * 1024 * 1024;
const uint64_t CHUNK_SIZE = 50 * 1024 * 1024;
const int TIMEOUT_MS = 5 * 1000;
int err_major;


/* hsm_copytool_private will hold an open FD on the lustre mount point
 * for us. This is to make sure it doesn't drop out from under us (and
 * remind the admin to shutdown the copytool before unmounting).
 */
struct hsm_copytool_private *ctdata;

/* everything else is zeroed */
struct ct_options ct_opt = {
    .o_verbose = LLAPI_MSG_INFO,
    .o_report_int = REPORT_INTERVAL_DEFAULT,
    .o_config = "config.cfg",
};

int should_retry(int *retry_count) {
    if ((*retry_count)--) {
        // Sleep before next retry; start out with a 1 second sleep
        static int retrySleepInterval = 1;
        sleep(retrySleepInterval);
        // Next sleep 1 second longer
        ++retrySleepInterval;
        return 1;
    }

    return 0;
}

double ct_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + 0.000001 * tv.tv_usec;
}

int ct_save_stripe(int src_fd, const char *src, strippingInfo *params) {
    char lov_buf[XATTR_SIZE_MAX];
    struct lov_user_md *lum;
    int rc;
    ssize_t xattr_size;

    assert(src && params);

    xattr_size = fgetxattr(src_fd, XATTR_LUSTRE_LOV, lov_buf, sizeof(lov_buf));
    if (xattr_size < 0) {
        rc = -errno;
        tlog_error("cannot get stripe info on '%s'", src);
        return rc;
    }

    lum = (struct lov_user_md *)lov_buf;

    params->lmm_stripe_size = lum->lmm_stripe_size;
    params->lmm_stripe_count = lum->lmm_stripe_count;

    return 0;
}

int ct_path_lustre(char *buf, int sz, const char *mnt, const lustre_fid *fid) {
    return snprintf(buf, sz, "%s/%s/fid/" DFID_NOBRACE, mnt, dot_lustre_name,
                    PFID(fid));
}

int ct_path_archive(char *buf, int sz, const lustre_fid *fid) {
    __u64 sequence_id = (fid)->f_seq;
    __u32 object_id = (fid)->f_oid;
    __u32 version = (fid)->f_ver;
    return snprintf(buf, sz, "%016llx_%08x_%08x", sequence_id, object_id,
                    version);
}

bool ct_is_retryable(int err) { return err == -ETIMEDOUT; }

int ct_action_done(struct hsm_copyaction_private **phcp,
                   const struct hsm_action_item *hai, int hp_flags, int ct_rc) {
    struct hsm_copyaction_private *hcp;
    char lstr[PATH_MAX];
    int rc;

    assert(hai);

    tlog_info("Action completed, notifying coordinator "
             "cookie=%#jx , FID="DFID", hp_flags=%d err=%d",
             (uintmax_t)hai->hai_cookie, PFID(&hai->hai_fid), hp_flags, -ct_rc);

    ct_path_lustre(lstr, sizeof(lstr), ct_opt.o_mnt, &hai->hai_fid);

    if (phcp == NULL || *phcp == NULL) {
        rc = llapi_hsm_action_begin(&hcp, ctdata, hai, -1, 0, true);
        if (rc < 0) {
            tlog_error("llapi_hsm_action_begin() on '%s' failed", lstr);
            return rc;
        }
        phcp = &hcp;
    }

    rc = llapi_hsm_action_end(phcp, &hai->hai_extent, hp_flags, abs(ct_rc));
    if (rc == -ECANCELED)
        tlog_error("completed action on '%s' has been canceled: "
                     "cookie=%#jx, FID="DFID,
                 lstr, (uintmax_t)hai->hai_cookie, PFID(&hai->hai_fid));
    else if (rc < 0)
        tlog_error("llapi_hsm_action_end() on '%s' failed", lstr);
    else
        tlog_info("llapi_hsm_action_end() on '%s' ok (rc=%d)", lstr, rc);

    return rc;
}

void handler(int signal) {
    psignal(signal, "exiting");
    /* If we don't clean up upon interrupt, umount thinks there's a ref
     * and doesn't remove us from mtab (EINPROGRESS). The lustre client
     * does successfully unmount and the mount is actually gone, but the
     * mtab entry remains. So this just makes mtab happier. */
    llapi_hsm_copytool_unregister(&ctdata);

    _exit(1);
}

int ct_begin_restore(struct hsm_copyaction_private **phcp,
                     const struct hsm_action_item *hai, int mdt_index,
                     int open_flags) {
    char src[PATH_MAX];
    int rc;

    assert(hai);

    rc =
        llapi_hsm_action_begin(phcp, ctdata, hai, mdt_index, open_flags, false);
    if (rc < 0) {
        ct_path_lustre(src, sizeof(src), ct_opt.o_mnt, &hai->hai_fid);
        tlog_error("llapi_hsm_action_begin() on '%s' failed", src);
    }

    return rc;
}

int ct_begin(struct hsm_copyaction_private **phcp,
             const struct hsm_action_item *hai) {
    /* Restore takes specific parameters. Call the same function w/ default
     * values for all other operations. */
    return ct_begin_restore(phcp, hai, -1, 0);
}

int ct_setup(void) {
    int rc;

    rc = llapi_search_fsname(ct_opt.o_mnt, fs_name);
    if (rc < 0) {
        tlog_error("cannot find a Lustre filesystem mounted at '%s'",
                 ct_opt.o_mnt);
        return rc;
    }

    ct_opt.o_mnt_fd = open(ct_opt.o_mnt, O_RDONLY);
    if (ct_opt.o_mnt_fd < 0) {
        rc = -errno;
        tlog_error("cannot open mount point at '%s'", ct_opt.o_mnt);
        return rc;
    }

    return rc;
}

int ct_cleanup(void) {
    int rc;

    if (ct_opt.o_mnt_fd >= 0) {
        rc = close(ct_opt.o_mnt_fd);
        if (rc < 0) {
            rc = -errno;
            tlog_error("cannot close mount point");
            return rc;
        }
    }

    return 0;
}

int ct_process_item(struct hsm_action_item *hai, const long hal_flags) {
    int rc = 0;
    assert(hai);

	char path[PATH_MAX];
	char file_name[PATH_MAX];
    if (ct_opt.o_verbose >= LLAPI_MSG_INFO || ct_opt.o_dry_run) {
        /* Print the original path */
        char fid[128];
        long long recno = -1;
        int linkno = 0;

        sprintf(fid, DFID, PFID(&hai->hai_fid));
        tlog_info("'%s' action %s reclen %d, cookie=%#jx", fid,
                 hsm_copytool_action2name(hai->hai_action), hai->hai_len,
                 (uintmax_t)hai->hai_cookie);

        // get file posix path from file identifier
        rc = llapi_fid2path(ct_opt.o_mnt, fid, path, sizeof(path), &recno, &linkno);
        if (rc < 0) {
            // copytool must get object/file name from path
            // failed to get path means failed to exec HSM command
            tlog_error("cannot get path of FID %s", fid);
            return rc;
        } else {
            tlog_info("processing file '%s'", path);
        }
    }

    char *ptr = strrchr(path, '/');
    if (ptr)
        strcpy(file_name, ptr + 1);
    else
        strcpy(file_name, path);

    switch (hai->hai_action) {
    /* set err_major, minor inside these functions */
    case HSMA_ARCHIVE:
        tlog_info("Start archive file '%s' to HSM backend", path);
        rc = ct_archive(hai, hal_flags, file_name);
        if (rc) {
            tlog_info("Failed to archive '%s' to HSM backend", path);
        } else {
            tlog_info("Success archive file '%s' to HSM backend", path);
        }
        break;

    case HSMA_RESTORE:
        tlog_info("Start restore file '%s' from HSM backend", path);
        rc = ct_restore(hai, hal_flags, file_name);
        if (rc) {
            tlog_info("Failed to restore '%s' from HSM backend", path);
        } else {
            tlog_info("Success restore file '%s' from HSM backend", path);
        }
        break;

    case HSMA_REMOVE:
        tlog_info("Start remove file '%s' from HSM backend", path);
        rc = ct_remove(hai, hal_flags, file_name);
        if (rc) {
            tlog_info("Failed to remove '%s' from HSM backend", path);
        } else {
            tlog_info("Success remove '%s' from HSM backend", path);
        }
        break;

    case HSMA_CANCEL:
        rc = ct_cancel(hai, hal_flags);
        break;

    default:
        rc = -EINVAL;
        tlog_error("unknown action %d, on '%s'", hai->hai_action,
                 ct_opt.o_mnt);
        ct_action_done(NULL, hai, 0, rc);
    }

    return 0;
}

void *ct_thread(void *data) {
    struct ct_th_data *cttd = data;
    int rc;

    rc = ct_process_item(cttd->hai, cttd->hal_flags);

    free(cttd->hai);
    free(cttd);
    pthread_exit((void *)(intptr_t)rc);
}

int ct_process_item_async(const struct hsm_action_item *hai, long hal_flags) {
    pthread_attr_t attr;
    pthread_t thread;
    struct ct_th_data *data;
    int rc;
    assert(hai);

    data = malloc(sizeof(*data));
    if (data == NULL)
        return -ENOMEM;

    data->hai = malloc(hai->hai_len);
    if (data->hai == NULL) {
        free(data);
        return -ENOMEM;
    }

    memcpy(data->hai, hai, hai->hai_len);
    data->hal_flags = hal_flags;

    rc = pthread_attr_init(&attr);
    if (rc != 0) {
        tlog_error("pthread_attr_init failed for '%s' service", ct_opt.o_mnt);
        free(data->hai);
        free(data);
        return -rc;
    }

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    rc = pthread_create(&thread, &attr, ct_thread, data);
    if (rc != 0)
        tlog_error("cannot create thread for '%s' service", ct_opt.o_mnt);

    pthread_attr_destroy(&attr);
    return 0;
}

/* Daemon waits for messages from the kernel; run it in the background. */
int ct_run(void) {
    int rc;

    if (ct_opt.o_daemonize) {
        rc = daemon(1, 1);
        if (rc < 0) {
            rc = -errno;
            tlog_error("cannot daemonize");
            return rc;
        }
    }

    rc = llapi_hsm_copytool_register(
        &ctdata, ct_opt.o_mnt, ct_opt.o_archive_cnt, ct_opt.o_archive_id, 0);
    if (rc < 0) {
        tlog_error("cannot start copytool interface");
        return rc;
    }

    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    while (1) {
        struct hsm_action_list *hal;
        struct hsm_action_item *hai;
        int msgsize;
        int i = 0;

        tlog_info("waiting for message from kernel");

        rc = llapi_hsm_copytool_recv(ctdata, &hal, &msgsize);
        if (rc == -ESHUTDOWN) {
            tlog_info("shutting down");
            break;
        } else if (rc < 0) {
            tlog_error("cannot receive action list: %d", -rc);
            err_major++;
            if (ct_opt.o_abort_on_error)
                break;
            else
                continue;
        }

        tlog_info("copytool fs=%s archive#=%d item_count=%d", hal->hal_fsname,
                 hal->hal_archive_id, hal->hal_count);

        if (strcmp(hal->hal_fsname, fs_name) != 0) {
            rc = -EINVAL;
            tlog_error("'%s' invalid fs name, expecting: %s", hal->hal_fsname,
                     fs_name);
            err_major++;
            if (ct_opt.o_abort_on_error)
                break;
            else
                continue;
        }

        hai = hai_first(hal);
        while (++i <= hal->hal_count) {
            if ((char *)hai - (char *)hal > msgsize) {
                rc = -EPROTO;
                tlog_error("'%s' item %d past end of message!", ct_opt.o_mnt, i);
                err_major++;
                break;
            }
            rc = ct_process_item_async(hai, hal->hal_flags);
            if (rc < 0)
                tlog_error("'%s' item %d process", ct_opt.o_mnt, i);
            if (ct_opt.o_abort_on_error && err_major)
                break;
            hai = hai_next(hai);
        }

        if (ct_opt.o_abort_on_error && err_major)
            break;
    }

    llapi_hsm_copytool_unregister(&ctdata);

    return rc;
}
