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
 * Copyright (c) 2024
 * Authors:
 *     Fu Qingyun <qingyun_fu@qq.com>,
 *
 * See README
 */
/* HSM copytool program for S3 object storage.
 *
 * An HSM copytool daemon acts on action requests from Lustre to copy files
 * to and from an HSM archive system.
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "lhsmtool_s3.h"
#include "ct_common.h"
#include "tlog.h"
#include "growbuffer.h"
#include "hsm_s3_utils.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/xattr.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libconfig.h>
#include <libs3.h>
#include <lz4.h>
#include <assert.h>
#include <openssl/md5.h>
#include <bsd/string.h> /* To get strlcpy */

char access_key[S3_MAX_KEY_SIZE];
char secret_key[S3_MAX_KEY_SIZE];
char host[S3_MAX_HOSTNAME_SIZE];
char bucket_name[S3_MAX_BUCKET_NAME_SIZE];

S3BucketContext bucketContext = {
    host,
    bucket_name,
    S3ProtocolHTTP,
    S3UriStylePath,
    access_key,
    secret_key
};

static int get_s3_object(char *objectName, get_object_callback_data *data,
                         S3GetObjectHandler *getObjectHandler) {

    assert(objectName && data && getObjectHandler);
    memset(data, 0, sizeof(get_object_callback_data));

    // Get a local copy of the general bucketContext than overwrite the
    // pointer to the bucket_name
    S3BucketContext localbucketContext;
    memcpy(&localbucketContext, &bucketContext, sizeof(S3BucketContext));
    localbucketContext.bucketName = bucket_name;

    data->buffer_offset = 0;
    data->buffer = NULL;

    double before_s3_get = ct_now();
    int retry_count = RETRYCOUNT;

    do {
        S3_get_object(&localbucketContext, objectName, NULL, 0, 0, NULL, 0,
                      getObjectHandler, data);
    } while (S3_status_is_retryable(data->status) &&
             should_retry(&retry_count));

    tlog_info("S3 get of %s took %fs", objectName, ct_now() - before_s3_get);

    if (data->buffer == NULL && data->totalLength != 0) {
        tlog_info("data->buffer == NULL");
        return -ENOMEM;
    }
    if (data->status != S3StatusOK) {
        tlog_error("S3Error %s", S3_get_status_name(data->status));
        return -EIO;
    }

    return 0;
}

static void usage(const char *name, int rc) {
    // TODO correct the usage help for s3
    fprintf(
        stdout,
        " Usage: %s [options]... <mode> <lustre_mount_point>\n"
        "The Lustre HSM S3 copy tool can be used as a daemon or "
        "as a command line tool\n"
        "The Lustre HSM daemon acts on action requests from Lustre\n"
        "to copy files to and from an HSM archive system.\n"
        "   --daemon            Daemon mode, run in background\n"
        " Options:\n"
        "The Lustre HSM tool performs administrator-type actions\n"
        "on a Lustre HSM archive.\n"
        "   --abort-on-error          Abort operation on major error\n"
        "   -A, --archive <#>         Archive number (repeatable)\n"
        "   -c, --config <path>       Path to the config file\n"
        "   --dry-run                 Don't run, just show what would be done\n"
        "   -q, --quiet               Produce less verbose output\n"
        "   -u, --update-interval <s> Interval between progress reports sent\n"
        "                             to Coordinator\n"
        "   -v, --verbose             Produce more verbose output\n",
        cmd_name);

    exit(rc);
}

static int ct_parseopts(int argc, char *const *argv) {
    struct option long_opts[] = {
        { "abort-on-error", no_argument, &ct_opt.o_abort_on_error, 1 },
        { "abort_on_error", no_argument, &ct_opt.o_abort_on_error, 1 },
        { "archive", required_argument, NULL, 'A' },
        { "config", required_argument, NULL, 'c' },
        { "daemon", no_argument, &ct_opt.o_daemonize, 1 },
        { "dry-run", no_argument, &ct_opt.o_dry_run, 1 },
        { "help", no_argument, NULL, 'h' },
        { "quiet", no_argument, NULL, 'q' },
        { "rebind", no_argument, NULL, 'r' },
        { "update-interval", required_argument, NULL, 'u' },
        { "update_interval", required_argument, NULL, 'u' },
        { "verbose", no_argument, NULL, 'v' },
        { 0, 0, 0, 0 }
    };
    int c, rc;
    config_t cfg;
    const char *config_str;

    optind = 0;
    while ((c = getopt_long(argc, argv, "A:b:c:hp:qu:v", long_opts, NULL)) !=
           -1) {
        switch (c) {
        case 'A':
            if ((ct_opt.o_archive_cnt >= LL_HSM_ORIGIN_MAX_ARCHIVE) ||
                (atoi(optarg) >= LL_HSM_ORIGIN_MAX_ARCHIVE)) {
                rc = -E2BIG;
                tlog_error("archive number must be less than %zu",
                         LL_HSM_ORIGIN_MAX_ARCHIVE);
                return rc;
            }
            ct_opt.o_archive_id[ct_opt.o_archive_cnt] = atoi(optarg);
            ct_opt.o_archive_cnt++;
            break;
        case 'b': /* -b and -c have both a number with unit as arg */
        case 'c':
            ct_opt.o_config = optarg;
            break;
        case 'h':
            usage(argv[0], 0);
        case 'q':
            ct_opt.o_verbose--;
            break;
        case 'u':
            ct_opt.o_report_int = atoi(optarg);
            if (ct_opt.o_report_int < 0) {
                rc = -EINVAL;
                tlog_error("bad value for -%c '%s'", c, optarg);
                return rc;
            }
            break;
        case 'v':
            ++ct_opt.o_verbose;
            break;
        case 0:
            break;
        default:
            return -EINVAL;
        }
    }

    if (argc != optind + 1) {
        rc = -EINVAL;
        tlog_error("no mount point specified");
        return rc;
    }

    ct_opt.o_mnt = argv[optind];
    ct_opt.o_mnt_fd = -1;

    tlog_info("mount_point=%s", ct_opt.o_mnt);

    config_init(&cfg);
    if (!config_read_file(&cfg, ct_opt.o_config)) {
        tlog_error("error while reading config file\r\n%s:%d - %s",
                 config_error_file(&cfg), config_error_line(&cfg),
                 config_error_text(&cfg));
        return -EINVAL;
    }

    if (config_lookup_string(&cfg, "access_key", &config_str)) {
        strncpy(access_key, config_str, sizeof(access_key));
    } else {
        tlog_error("could not find access_key");
        return -EINVAL;
    }

    if (config_lookup_string(&cfg, "secret_key", &config_str)) {
        strncpy(secret_key, config_str, sizeof(secret_key));
    } else {
        tlog_error("could not find secret_key");
        return -EINVAL;
    }

    if (config_lookup_string(&cfg, "host", &config_str)) {
        strncpy(host, config_str, sizeof(host));
    } else {
        tlog_error("could not find host");
        return -EINVAL;
    }

    if (config_lookup_string(&cfg, "bucket_name", &config_str)) {
        strncpy(bucket_name, config_str, sizeof(host));
    } else {
        tlog_error("could not find bucket_name");
        return -EINVAL;
    }

    int ssl_enabled;
    if (config_lookup_bool(&cfg, "ssl", &ssl_enabled)) {
        if (ssl_enabled) {
            bucketContext.protocol = S3ProtocolHTTPS;
        } else {
            bucketContext.protocol = S3ProtocolHTTP;
        }
    } else {
        tlog_error("could not find ssl");
        return -EINVAL;
    }

    return 0;
}

static int fid_parent(const char *mnt, const lustre_fid *fid, char *parent,
              size_t parent_len)
{
    int         rc;
    int         linkno = 0;
    long long     recno = -1;
    char         file[PATH_MAX];
    char         strfid[FID_NOBRACE_LEN + 1];
    char        *ptr;

    snprintf(strfid, sizeof(strfid), DFID_NOBRACE, PFID(fid));

    rc = llapi_fid2path(mnt, strfid, file, sizeof(file),
                &recno, &linkno);
    if (rc < 0)
        return rc;

    /* fid2path returns a relative path */
    rc = snprintf(parent, parent_len, "%s/%s", mnt, file);
    if (rc >= parent_len)
        return -ENAMETOOLONG;

    /* remove file name */
    ptr = strrchr(parent, '/');
    if (ptr == NULL || ptr == parent) {
        rc = -EINVAL;
    } else {
        *ptr = '\0';
        rc = 0;
    }

    return rc;
}

static bool ct_archive_check(const struct hsm_action_item *hai, const char *src,
                             const int src_fd, struct stat *src_st)
{
    bool rc = true;

    // Archiving a file from Lustre to the object store
    if (fstat(src_fd, src_st) < 0) {
        rc = false;
        tlog_error("cannot stat '%s'");
        return rc;
    }

    if (!S_ISREG(src_st->st_mode)) {
        rc = false;
        tlog_error("'%s' is not a regular file", src);
        return rc;
    }

    if (hai->hai_extent.offset > (__u64)src_st->st_size) {
        rc = false;
        tlog_error("Trying to start reading past end (%ju > "
                     "%jd) of '%s' source file",
                 (uintmax_t)hai->hai_extent.offset, (intmax_t)src_st->st_size, src);
        return rc;
    }

    return rc;
}

static void ct_mk_put_properties(S3PutProperties *obj_put_properties)
{
    static char octet_mime_string[] = "binary/octet-stream";

    memset(obj_put_properties, 0, sizeof(S3PutProperties));
    obj_put_properties->contentType = octet_mime_string;
    // object never expires
    obj_put_properties->expires = -1;
}

static int ct_archive_data(struct hsm_copyaction_private *hcp, const char *src,
                           const char *dst, int src_fd, struct stat *src_st,
                           const struct hsm_action_item *hai, long hal_flags) {
    #ifndef UNIT_TEST
    struct hsm_extent he;
    time_t last_report_time;
    char *dbuf = NULL;
    __u64 file_offset = hai->hai_extent.offset;
    __u64 length = hai->hai_extent.length;
    #else
    __u64 file_offset = 0;
    char *dbuf = NULL;
    __u64 length = src_st->st_size;
    #endif
    int rc = 0;
    double start_ct_now = ct_now();
    time_t now;

#ifndef UNIT_TEST
    // TODO: current code has not put striping info into object meta part
    // if we need it, or use default striping setting in lustre directory when restore data ?
    strippingInfo stripping_params;
    stripping_params.lmm_stripe_count = 1;
    stripping_params.lmm_stripe_size = ONE_MB;

    if (ct_save_stripe(src_fd, src, &stripping_params)) {
        return -1;
    }

    /* Don't read beyond a given extent */
    if (length > src_st->st_size - hai->hai_extent.offset)
        length = src_st->st_size - hai->hai_extent.offset;

    last_report_time = time(NULL);

    he.offset = file_offset;
    he.length = 0;
    rc = llapi_hsm_action_progress(hcp, &he, length, 0);
    if (rc < 0) {
        /* Action has been canceled or something wrong
         * is happening. Stop copying data. */
        tlog_error("progress ioctl for copy '%s'->'%s' failed", src, dst);
        goto out;
    }
#endif

    dbuf = malloc(src_st->st_size);
    if (dbuf == NULL) {
        rc = -ENOMEM;
        goto out;
    }

    // setup properities for put object
    S3PutProperties putProperties;
    ct_mk_put_properties(&putProperties);

    put_object_callback_data data;
    memset(&data, 0, sizeof(put_object_callback_data));

    double before_lustre_read = ct_now();
    pread(src_fd, dbuf, length, file_offset);
    tlog_info(
        "Reading data from %s of %llu bytes offset %llu from lustre "
        "took %fs",
        src, length, file_offset, ct_now() - before_lustre_read);

    data.buffer = dbuf;
    data.contentLength = length;

    S3PutObjectHandler putObjectHandler = { putResponseHandler,
                                            &putObjectDataCallback };

    const char *obj_name = dst;

    // Get a local copy of the general bucketContext than overwrite the
    // pointer to the bucket_name
    S3BucketContext localbucketContext;
    memcpy(&localbucketContext, &bucketContext, sizeof(S3BucketContext));

    localbucketContext.bucketName = bucket_name;

    double before_s3_put = ct_now();
    int retry_count = RETRYCOUNT;
    do {
        S3_put_object(&localbucketContext, obj_name, length,
                      &putProperties, NULL, 0, &putObjectHandler, &data);
    } while (S3_status_is_retryable(data.status) &&
             should_retry(&retry_count));
    tlog_info("S3 put of '%s' to bucket '%s' took %fs", obj_name, bucket_name, ct_now() - before_s3_put);

    if (data.status != S3StatusOK) {
        rc = -EIO;
        tlog_error("S3Error %s", S3_get_status_name(data.status));
        goto out;
    }

#ifndef UNIT_TEST
    now = time(NULL);
    if (now >= last_report_time + ct_opt.o_report_int) {
        tlog_info("sending progress report for archiving %s", src);
        he.offset = file_offset;
        he.length = length;
        rc = llapi_hsm_action_progress(hcp, &he, length, 0);
        if (rc < 0) {
            /* Action has been canceled or something wrong
             * is happening. Stop copying data. */
            tlog_error("progress ioctl for copy '%s'->'%s' failed", src, dst);
            goto out;
        }
    }
#endif

    rc = 0;
out:
    if (dbuf != NULL)
        free(dbuf);
    tlog_info("copied %ju bytes in %f seconds", length, ct_now() - start_ct_now);

    return rc;
}

static int ct_archive_data_big (struct hsm_copyaction_private *hcp, const char *src,
                                const char *object_name, int src_fd, struct stat *src_st,
                                const struct hsm_action_item *hai, long hal_flags) {
	#ifndef UNIT_TEST
    struct hsm_extent he;
    time_t last_report_time;
    char *dbuf = NULL;
    __u64 file_offset = hai->hai_extent.offset;
    __u64 length = hai->hai_extent.length;
	#else
    __u64 file_offset = 0;
    char *dbuf = NULL;
    __u64 length = 0;
	#endif
    int rc = 0;
    double start_ct_now = ct_now();
    time_t now;

#ifndef UNIT_TEST
    // TODO: current code has not put striping info into object meta part
    // if we need it, or use default striping setting in lustre directory when restore data ?
    strippingInfo stripping_params;
    stripping_params.lmm_stripe_count = 1;
    stripping_params.lmm_stripe_size = ONE_MB;

    if (ct_save_stripe(src_fd, src, &stripping_params)) {
        return -1;
    }

    /* Don't read beyond a given extent */
    if (length > src_st->st_size - hai->hai_extent.offset)
        length = src_st->st_size - hai->hai_extent.offset;

    last_report_time = time(NULL);

    he.offset = file_offset;
    he.length = 0;
    rc = llapi_hsm_action_progress(hcp, &he, length, 0);
    if (rc < 0) {
        /* Action has been canceled or something wrong
         * is happening. Stop copying data. */
        tlog_error("progress ioctl for archive '%s'", object_name);
        goto out;
    }
#endif
    // setup properities for put object
    S3PutProperties putProperties;
    ct_mk_put_properties(&putProperties);

    put_object_callback_data data;
    memset(&data, 0, sizeof(put_object_callback_data));

    double	  before_lustre_read = ct_now();
    size_t	  contentLength	     = src_st->st_size;
    size_t	  totalContentLength = src_st->st_size;
    size_t	  todoContentLength  = src_st->st_size;

    UploadManager manager;
    manager.upload_id = NULL;
    manager.gb	      = NULL;

    // get total chunk count for multipart upload
    int total_seq = ((src_st->st_size + CHUNK_SIZE - 1) / CHUNK_SIZE);

    manager.etags = (char **)malloc(sizeof(char *) * total_seq);
    manager.next_etags_pos = 0;

    const char *uploadId = 0;
    int retry_count = RETRYCOUNT;
    bool is_retryable;
    do {
        S3_initiate_multipart(&bucketContext, object_name, 0, &multipart_init_handler,
                              NULL, TIMEOUT_MS, &manager);
        S3Status rc = (manager.upload_id == NULL) ? S3StatusErrorInternalError : 0;
        is_retryable = S3_status_is_retryable(rc);
    } while (is_retryable && should_retry(&retry_count));

    // TODO: read AWS API DOC, if upload_id always not 0 when where have no error happed
    if (manager.upload_id == NULL) {
	    tlog_error(
		    "failed to initiate multipart upload for object '%s' on bucket '%s'",
		    object_name, bucket_name);
	    goto clean;
    }

    assert(manager.gb == NULL);

    // prepare file handle for multi part upload
    data.infile = fopen(src, "rb");
    if (data.infile == NULL) {
	    rc = EIO;
	    tlog_error("ERROR: Failed to open input file %s: ", src);
	    goto clean;
    }

    // multi part upload start
    int partContentLength = 0;
    MultipartPartData part_data;
    memset(&part_data, 0, sizeof(MultipartPartData));

	todoContentLength -= CHUNK_SIZE * manager.next_etags_pos;
	for (int seq = manager.next_etags_pos + 1; seq <= total_seq; seq++) {
		part_data.manager = &manager;
		part_data.seq	 = seq;
		if (part_data.put_object_data.gb == NULL) {
			part_data.put_object_data = data;
		}
		partContentLength = ((contentLength > CHUNK_SIZE) ?
					     CHUNK_SIZE :
					     contentLength);
		tlog_info("%s Part Seq %d, length=%d start", src, seq,
			  partContentLength);
		part_data.put_object_data.contentLength = partContentLength;
		part_data.put_object_data.originalContentLength =
			partContentLength;
		part_data.put_object_data.totalContentLength = todoContentLength;
		part_data.put_object_data.totalOriginalContentLength =
			totalContentLength;
		putProperties.md5 = 0;
		int retry_count	  = RETRYCOUNT;

		do {
			S3_upload_part(&bucketContext, object_name, &putProperties,
				       &multipart_upload_part_handler, seq,
				       manager.upload_id, partContentLength, NULL,
				       TIMEOUT_MS, &part_data);
		} while (S3_status_is_retryable(
				 part_data.put_object_data.status) &&
			 should_retry(&retry_count));

		if (part_data.put_object_data.status != S3StatusOK) {
			tlog_error(
				"failed to put Part Seq of %d, for object '%s'",
				seq, object_name);
			goto clean;
		}
		contentLength -= CHUNK_SIZE;
		todoContentLength -= CHUNK_SIZE;
        assert(manager.gb == NULL);
	}

	int size = 0;
	size += growbuffer_append(&(manager.gb), "<CompleteMultipartUpload>",
				  strlen("<CompleteMultipartUpload>"));

	for (int i = 0, n = 0; i < total_seq; i++) {
        char buf[ 256 ];
		n = snprintf(buf, sizeof(buf),
			     "<Part><PartNumber>%d</PartNumber>"
			     "<ETag>%s</ETag></Part>",
			     i + 1, manager.etags[ i ]);
		size += growbuffer_append(&(manager.gb), buf, n);
	}
	size += growbuffer_append(&(manager.gb), "</CompleteMultipartUpload>",
				  strlen("</CompleteMultipartUpload>"));
	manager.remaining = size;

	retry_count = RETRYCOUNT;
	do {
		S3_complete_multipart_upload(&bucketContext, object_name,
					     &multipart_commit_handler, manager.upload_id,
					     manager.remaining, NULL, TIMEOUT_MS,
					     &manager);
	} while (S3_status_is_retryable(manager.remaining) &&
		 should_retry(&retry_count));
	if (manager.remaining) {
		tlog_error(
			"failed to complete multipart upload of %d, for object '%s'",
			manager.upload_id, object_name);
		goto clean;
	}

#ifndef UNIT_TEST
    now = time(NULL);
    if (now >= last_report_time + ct_opt.o_report_int) {
        tlog_info("sending progress report for archiving %s", src);
        he.offset = file_offset;
        he.length = length;
        rc = llapi_hsm_action_progress(hcp, &he, length, 0);
        if (rc < 0) {
            /* Action has been canceled or something wrong
             * is happening. Stop copying data. */
            tlog_error("progress ioctl for copy '%s'->'%s' failed", src, object_name);
            goto out;
        }
    }
#endif

clean:
	if (manager.upload_id) {
		free(manager.upload_id);
	}
	for (int i = 0; i < manager.next_etags_pos; i++) {
		free(manager.etags[ i ]);
	}
	growbuffer_destroy(manager.gb);
	free(manager.etags);

out:
	if (dbuf != NULL)
		free(dbuf);

	if (!rc) {
		tlog_info("copied %ju bytes in %f seconds", src_st->st_size,
			  ct_now() - start_ct_now);
	} else {
		tlog_error("failed to upload %s", object_name);
	}

	return rc;
}

static int ct_restore_data(struct hsm_copyaction_private *hcp, const char *src,
                           const char *dst, int dst_fd,
                           const struct hsm_action_item *hai, long hal_flags, char *file_name) {
    double start_ct_now = ct_now();
    #ifndef UNIT_TEST
    struct hsm_extent he;
    __u64 file_offset = hai->hai_extent.offset;
    struct stat dst_st;
    __u64 write_total = 0;
    __u64 length = hai->hai_extent.length;
    time_t last_report_time;
    time_t now;
    int rc = 0;

    // Restore a file from the object store back to Lustre
    if (fstat(dst_fd, &dst_st) < 0) {
        rc = -errno;
        tlog_error("cannot stat '%s'", dst);
        return rc;
    }

    if (!S_ISREG(dst_st.st_mode)) {
        rc = -EINVAL;
        tlog_error("'%s' is not a regular file", dst);
        return rc;
    }

    he.offset = file_offset;
    he.length = 0;
    rc = llapi_hsm_action_progress(hcp, &he, length, 0);
    if (rc < 0) {
        /* Action has been canceled or something wrong
         * is happening. Stop copying data. */
        tlog_error("progress ioctl for copy '%s'->'%s' failed", src, dst);
        goto out;
    }

    last_report_time = time(NULL);
    #else
    __u64 file_offset = 0;
    struct stat dst_st;
    __u64 length = -1;
    int rc = 0;
    #endif

    // Downloading from the object store
    char *object_name = file_name;

    S3GetObjectHandler getObjectHandler = { getResponseHandler,
                                            &getObjectDataCallback };

    if (length == -1) {
        if (file_offset == 0) {
            // Download data and metadata from the first chunk
            get_object_callback_data data;
            rc = get_s3_object(object_name, &data, &getObjectHandler);
            if (rc < 0) {
                if (data.buffer != NULL)
                    free(data.buffer);
                goto out;
            }

            length = data.contentLength;
            double before_lustre_write = ct_now();
            pwrite(dst_fd, data.buffer, data.contentLength, 0);
            tlog_info("Writing %s of %llu bytes offset %llu to "
                     "lustre "
                     "took %fs",
                     object_name, length, file_offset,
                     ct_now() - before_lustre_write);

            if (data.buffer != NULL)
                free(data.buffer);
        } else {
            tlog_error("Invalid HSM request");
            assert(0);
        }
    } else {
        tlog_error("Invalid initial value of hai->hai_extent.length %lu", length);
        assert(0);
    }

#ifndef UNIT_TEST
    now = time(NULL);
    if (now >= last_report_time + ct_opt.o_report_int) {
        tlog_info("sending progress report for archiving %s", src);
        he.offset = file_offset;
        he.length = length;
        rc = llapi_hsm_action_progress(hcp, &he, length, 0);
        if (rc < 0) {
            /* Action has been canceled or something wrong
             * is happening. Stop copying data. */
            tlog_error("progress ioctl for copy '%s'->'%s' failed", src, dst);
            goto out;
        }
    }
#endif

out:
    tlog_info("copied %jd bytes in %f seconds", (uintmax_t)length,
             ct_now() - start_ct_now);
    return rc;
}

int ct_archive(const struct hsm_action_item *hai, const long hal_flags, char *file_name) {
    struct hsm_copyaction_private *hcp = NULL;
    char src[PATH_MAX];
    int rc;
    int rcf = 0;
    int hp_flags = 0;
    int open_flags;
    int src_fd = -1;

    rc = ct_begin(&hcp, hai);
    if (rc < 0)
        goto end_ct_archive;

    /* we fill archive so:
     * source = data FID
     * destination = lustre FID
     */
    ct_path_lustre(src, sizeof(src), ct_opt.o_mnt, &hai->hai_dfid);

    char  parent[PATH_MAX];
    memset(parent, sizeof(parent), 0);
    rc = fid_parent(ct_opt.o_mnt, &hai->hai_fid, parent, PATH_MAX);

    if (ct_opt.o_dry_run) {
        rc = 0;
        goto end_ct_archive;
    }

    src_fd = llapi_hsm_action_get_fd(hcp);
    if (src_fd < 0) {
        rc = src_fd;
        tlog_error("cannot open '%s' for read", src);
        goto end_ct_archive;
    }

    open_flags = O_WRONLY | O_NOFOLLOW;
    /* If extent is specified, don't truncate an old archived copy */
    open_flags |= ((hai->hai_extent.length == -1) ? O_TRUNC : 0) | O_CREAT;

    struct stat src_st;
    if (ct_archive_check(hai, src, src_fd, &src_st) == false)
    {
        rc = -1;
        goto end_ct_archive;
    }

    if (src_st.st_size > (5LL * 1024 * 1024 * 1024 * 1024))
    {
        rc = -EIO;
        tlog_error("cannot copy '%s', file size too big than 5TB", src);
        goto end_ct_archive;
    }
    else
    {
        if (src_st.st_size >= MAX_OBJ_SIZE_LEVEL)
        {
            rc = ct_archive_data_big(hcp, src, file_name, src_fd, &src_st, hai, hal_flags);
        }
        else
        {
            rc = ct_archive_data(hcp, src, file_name, src_fd, &src_st, hai, hal_flags);
        }
    }

end_ct_archive:
    err_major++;

    if (ct_is_retryable(rc))
        hp_flags |= HP_FLAG_RETRY;

    rcf = rc;

    if (!(src_fd < 0))
        close(src_fd);

    // use '|', instead of '=', otherwise rc man become 0
    // may be ct_action_done can pass rcf value to rc, but cannot 100% sure from code
    // or can say it cannot (perhaps it because I cannot 100% understand code)
    // lustre/utils/liblustreapi_hsm.c
    rc |= ct_action_done(&hcp, hai, hp_flags, rcf);

    return rc;
}

int ct_restore(const struct hsm_action_item *hai, const long hal_flags, char *path) {
    struct hsm_copyaction_private *hcp = NULL;
    struct lu_fid dfid;
    char src[PATH_MAX];
    char dst[PATH_MAX];
    int rc;
    int hp_flags = 0;
    int dst_fd = -1;
    int mdt_index = -1;
    int open_flags = 0;
    /* we fill lustre so:
     * source = lustre FID in the backend
     * destination = data FID = volatile file
     */

    /* build backend file name from released file FID */
    ct_path_archive(src, sizeof(src), &hai->hai_fid);

    rc = llapi_get_mdt_index_by_fid(ct_opt.o_mnt_fd, &hai->hai_fid, &mdt_index);
    if (rc < 0) {
        tlog_error("cannot get mdt index " DFID "", PFID(&hai->hai_fid));
        return rc;
    }

    rc = ct_begin_restore(&hcp, hai, mdt_index, open_flags);
    if (rc < 0)
        goto end_ct_restore;

    /* get the FID of the volatile file */
    rc = llapi_hsm_action_get_dfid(hcp, &dfid);
    if (rc < 0) {
        tlog_error("restoring " DFID ", cannot get FID of created volatile file",
                 PFID(&hai->hai_fid));
        goto end_ct_restore;
    }

    /* build volatile "file name", for messages */
    snprintf(dst, sizeof(dst), "{VOLATILE}=" DFID, PFID(&dfid));

    if (ct_opt.o_dry_run) {
        rc = 0;
        goto end_ct_restore;
    }

    dst_fd = llapi_hsm_action_get_fd(hcp);
    if (dst_fd < 0) {
        rc = dst_fd;
        tlog_error("cannot open '%s' for write", dst);
        goto end_ct_restore;
    }

    rc = ct_restore_data(hcp, src, dst, dst_fd, hai, hal_flags, path);
    if (rc < 0) {
        tlog_error("cannot restore '%s'", path);
        err_major++;
        if (ct_is_retryable(rc))
            hp_flags |= HP_FLAG_RETRY;
        goto end_ct_restore;
    }

end_ct_restore:
    rc = ct_action_done(&hcp, hai, hp_flags, rc);

    /* object swaping is done by cdt at copy end, so close of volatile file
     * cannot be done before */

    if (!(dst_fd < 0))
        close(dst_fd);

    return rc;
}

int ct_remove(const struct hsm_action_item *hai, const long hal_flags, char *file_name) {
    struct hsm_copyaction_private *hcp = NULL;
    char dst[PATH_MAX];
    int rc;
    int retry_count;
    char *object_name = file_name;

    #ifndef UNIT_TEST
    rc = ct_begin(&hcp, hai);
    if (rc < 0)
        goto end_ct_remove;

    if (ct_opt.o_dry_run) {
        rc = 0;
        goto end_ct_remove;
    }
    #endif

    retry_count = RETRYCOUNT;
    get_object_callback_data delete_data;

    // Get a local copy of the general bucketContext than overwrite the
    // pointer to the bucket_name
    S3BucketContext localbucketContext;
    memcpy(&localbucketContext, &bucketContext, sizeof(S3BucketContext));
    localbucketContext.bucketName = bucket_name;

    retry_count = RETRYCOUNT;
    do {
	    S3_delete_object(&localbucketContext, object_name, NULL, 0,
			     &deleteResponseHandler, &delete_data);
    } while (S3_status_is_retryable(delete_data.status) &&
	     should_retry(&retry_count));

    if (delete_data.status != S3StatusOK) {
	    rc = -EIO;
	    tlog_error("S3Error %s", S3_get_status_name(delete_data.status));
	    goto end_ct_remove;
    }

end_ct_remove:
    #ifndef UNIT_TEST
    rc = ct_action_done(&hcp, hai, 0, rc);
    #endif

    return rc;
}

int ct_cancel(const struct hsm_action_item *hai, const long hal_flags) {
    tlog_info("cancel not implemented for file system '%s'", ct_opt.o_mnt);
    /* Don't report progress to coordinator for this cookie:
     * the copy function will get ECANCELED when reporting
     * progress. */
    return 0;
}

static int ct_s3_cleanup(void) {
    int rc = 0;

    rc = ct_cleanup();
    if (rc == 0) {
        S3_deinitialize();
    }

    return rc;
}

#ifdef UNIT_TEST
int main(int argc, char **argv) {
    int rc;
    const int log_size_max = 1024 * 1024 * 10;
    const int log_max = 10;

    // initialize log parameters
    rc = tlog_init("/var/log/hsm_s3copytool.log", log_size_max, log_max, 0, 0);
    if (rc) return rc;

    strlcpy(cmd_name, basename(argv[0]), sizeof(cmd_name));

	tlog_setlevel(TLOG_DEBUG);

    rc = S3_initialize(NULL, S3_INIT_ALL, host);
    if (rc != 0) {
        tlog_error("Error in S3 init");
        goto error_cleanup;
    }

    strcpy(access_key, "123");
    strcpy(secret_key, "123");
    strcpy(host, "10.10.10.19:4950");
    strcpy(bucket_name, "test");

    #define BIG_FILE_UPLOAD 1
    #define FILE_UPLOAD     2
    #define FILE_REMOVE     3
    int test_case = BIG_FILE_UPLOAD;

    char test_file[PATH_MAX];

    switch (test_case)
    {
        case BIG_FILE_UPLOAD:
            strcpy(test_file, "/root/AlmaLinux-8.8-x86_64-dvd.iso");
            break;

        case FILE_UPLOAD:
            strcpy(test_file, "/root/test.dat");
            break;

        case FILE_REMOVE:
            strcpy(test_file, "/root/initial-setup-ks.cfg");
            break;

        default:
            abort();
            break;
    }

    char *dst = rindex(test_file, (int)'/') + 1;
	struct hsm_copyaction_private *hcp = NULL;
	char *src = test_file;
	int src_fd;
	struct stat src_st;
	struct hsm_action_item *hai = NULL;
	long hal_flags = 0;
	stat(test_file, &src_st);

    switch (test_case)
    {
        case BIG_FILE_UPLOAD:
            rc = ct_archive_data_big(hcp, src, dst, src_fd, &src_st, hai, hal_flags);
            break;

        case FILE_UPLOAD:
            rc = ct_archive_data(hcp, src, dst, src_fd, &src_st, hai, hal_flags);
            break;

        case FILE_REMOVE:
            // upload then remove
            rc = ct_archive_data(hcp, src, dst, src_fd, &src_st, hai, hal_flags);
            rc = ct_remove(hai, hal_flags, dst);
            break;

        default:
            abort();
            break;
    }

error_cleanup:
    ct_s3_cleanup();

error_exit:
    tlog_exit();

    return -rc;
}

#else
int main(int argc, char **argv) {
    int rc;
    const int log_size_max = 1024 * 1024 * 10;
    const int log_max = 10;

    // initialize log parameters
    rc = tlog_init("/var/log/hsm_s3copytool.log", log_size_max, log_max, 0, 0);
    if (rc) return rc;

    strlcpy(cmd_name, basename(argv[0]), sizeof(cmd_name));
    rc = ct_parseopts(argc, argv);
    if (rc < 0) {
        tlog_error("try '%s --help' for more information", cmd_name);
        goto error_exit;
    }

    // reuse verbose option as log level control
    if (ct_opt.o_verbose >= LLAPI_MSG_INFO)
    {
        tlog_setlevel(TLOG_DEBUG);
    } else {
        tlog_setlevel(TLOG_ERROR);
    }

    rc = ct_setup();
    if (rc < 0)
        goto error_cleanup;

    rc = S3_initialize(NULL, S3_INIT_ALL, host);
    if (rc != 0) {
        tlog_error("Error in S3 init");
        goto error_cleanup;
    }

    rc = ct_run();

error_cleanup:
    ct_s3_cleanup();

error_exit:
    tlog_exit();

    return -rc;
}
#endif

