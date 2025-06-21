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
#include <assert.h>
#include <openssl/md5.h>
#include <bsd/string.h> /* To get strlcpy */
#include <sys/resource.h>

char access_key[S3_MAX_KEY_SIZE];
char secret_key[S3_MAX_KEY_SIZE];
char host[S3_MAX_HOSTNAME_SIZE];
char bucket_name[S3_MAX_BUCKET_NAME_SIZE];
char path_prefix[PATH_MAX];

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

    // Get a local copy of the general bucketContext than overwrite the
    // pointer to the bucket_name
    S3BucketContext localbucketContext;
    memcpy(&localbucketContext, &bucketContext, sizeof(S3BucketContext));
    localbucketContext.bucketName = bucket_name;

    double before_s3_get = ct_now();
    int retry_count = RETRYCOUNT;
    uint64_t startByte = 0, byteCount = 0;

    do {
        // always seek to offset (0), otherwise retry will lead to data corruption
        lseek(data->fd, 0, SEEK_SET);

        S3_get_object(&localbucketContext, objectName, NULL, startByte, byteCount, NULL, 0,
                      getObjectHandler, data);
    } while (S3_status_is_retryable(data->status) &&
             should_retry(&retry_count));

    tlog_info("S3 get of %s took %fs", objectName, ct_now() - before_s3_get);

    if (data->status != S3StatusOK) {
        tlog_error("S3Error %s", S3_get_status_name(data->status));
        return -EIO;
    }

    return 0;
}

static void ct_opt_setup(struct ct_options *opt_ptr)
{
    memset(opt_ptr, 0, sizeof(struct ct_options));
    opt_ptr->o_verbose = LLAPI_MSG_INFO;
    opt_ptr->o_report_int = REPORT_INTERVAL_DEFAULT;
}

static void ct_opt_dump(struct ct_options *opt_ptr)
{
    tlog_debug("dump copytool option for config file: %s", opt_ptr->o_config);
    tlog_debug("copytool abort_on_error: %s", opt_ptr->o_abort_on_error ? "no" : "yes");
    tlog_debug("copytool debug log: %s", opt_ptr->o_verbose ? "on" : "off");
    tlog_debug("copytool archive_cnt: %d", opt_ptr->o_archive_cnt);
    tlog_debug("copytool archive_id: %d", opt_ptr->o_archive_id[opt_ptr->o_archive_cnt]);
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
            if (atol(optarg) <= 0 || atol(optarg) > INT_MAX)
            {
                 rc = -E2BIG;
                 tlog_error("archive number must be lies between 1 to %d", INT_MAX);
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
        strncpy(bucket_name, config_str, sizeof(bucket_name));
        tlog_debug("use bucket of %s", bucket_name);
    } else {
        tlog_error("could not find bucket_name");
        return -EINVAL;
    }

    if (config_lookup_string(&cfg, "path_prefix", &config_str)) {
        strncpy(path_prefix, config_str, sizeof(path_prefix));
        tlog_debug("use path_prefix of %s", path_prefix);
    } else {
        tlog_warn("could not find path_prefix");
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

    if (config_lookup_int(&cfg, "max_requests", &max_requests)) {
        if (max_requests > 0)
            tlog_debug("use max_requests of %d", max_requests);
        else {
            tlog_error("invalid max_requests value %d in config file", max_requests);
            return -EINVAL;
        }
    } else {
        tlog_warn("could not find max_requests in config file, use default value of %u", MAX_HSM_REQUESTS);
        max_requests = MAX_HSM_REQUESTS;
    }

    return 0;
}

static void ct_dumpopts(int argc, char *const *argv) {
    char opts_cmd[PATH_MAX];

    memset(opts_cmd, 0, sizeof(opts_cmd));
    for (int i = 0; i < argc; i++)
    {
        sprintf(opts_cmd, "%s%s ", opts_cmd, argv[i]);
    }
    tlog_debug("dump command line\n %s", opts_cmd);
}

static int fid_parent(const char *mnt, const lustre_fid *fid, char *parent,
              size_t parent_len)
{
    int         rc;
    int         linkno = 0;
    long long   recno = -1;
    char        file[PATH_MAX];
    char        strfid[FID_NOBRACE_LEN + 1];
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

static char* ct_target(const char *full_path)
{
    // example:
    // full_path        "/mnt/fs001/lx005/license"
    // path_prefix      "/mnt/fs001/lx005"
    // action_target    "license"
    char *action_target = NULL;
    char *obj_pos = strstr(full_path, path_prefix);
    if (obj_pos && (obj_pos == full_path))
    {
        action_target = obj_pos + strlen(path_prefix) + 1;
    }

    return action_target;
}

static int ct_archive_data(struct hsm_copyaction_private *hcp, const char *src,
						   const char *object_name, int src_fd, struct stat *src_st,
                           const struct hsm_action_item *hai, long hal_flags) {
    struct hsm_extent he;
    time_t last_report_time;
    char *dbuf = NULL;
    __u64 length = hai->hai_extent.length;
    int rc = 0;
    double start_ct_now = ct_now();
    time_t now;

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

    he.offset = 0;
    he.length = 0;
    rc = llapi_hsm_action_progress_ex(hcp, &he, length, 0);
    if (rc < 0) {
        // only log warning for failed to send progress report
        tlog_warn("progress ioctl for copy '%s'->'%s' failed", src, object_name);
    }

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
    ssize_t rc_read = read(src_fd, dbuf, length);
    if (rc_read != (ssize_t)length)
    {
        tlog_debug("HSM request file %s with size %u from offset %u",
                    object_name, src_st->st_size, hai->hai_extent.offset);
        tlog_error("failed to read data from %s, request size %ld, get size %ld",
                    object_name, (ssize_t)length, rc_read);

        rc = -EIO;
        goto out;
    } else {
        tlog_info("Reading data from %s of %llu bytes took %fs", src, length,
                  ct_now() - before_lustre_read);
    }

    data.buffer = dbuf;
    data.contentLength = length;
    data.file_name = (char *)object_name;

    S3PutObjectHandler putObjectHandler = { putResponseHandler,
                                            &put_objectdata_callback
                                          };

    // Get a local copy of the general bucketContext than overwrite the
    // pointer to the bucket_name
    S3BucketContext localbucketContext;
    memcpy(&localbucketContext, &bucketContext, sizeof(S3BucketContext));

    localbucketContext.bucketName = bucket_name;

    double before_s3_put = ct_now();
    int retry_count = RETRYCOUNT;
    while (true)
    {
        tlog_debug("begin put '%s' to bucket '%s'", object_name, bucket_name);
        S3_put_object(&localbucketContext, object_name, length,
                      &putProperties, NULL, 0, &putObjectHandler, &data);
        if (data.status != S3StatusOK)
        {
            tlog_debug("failed to put '%s' to bucket '%s' with error code '%d'",
                        object_name, bucket_name, data.status);
            if (S3_status_is_retryable(data.status) && should_retry(&retry_count))
            {
                continue;
            } else {
                break;
            }
        } else {
            tlog_debug("put '%s' to bucket '%s' took %fs", object_name, bucket_name, ct_now() - before_s3_put);
            break;
        }
    }

    if (data.status != S3StatusOK) {
        rc = -EIO;
        tlog_error("S3Error %s", S3_get_status_name(data.status));
        goto out;
    }

    rc = 0;
out:
    if (dbuf != NULL)
        free(dbuf);
    tlog_info("copied %ju bytes in %f seconds", length, ct_now() - start_ct_now);

    return rc;
}

// get chunk size and chunk count for multipart upload
static void ct_get_chunksize(size_t s3_obj_size, size_t *s3_chunk_sz, size_t *s3_seq_total)
{
    size_t chunk_size = CHUNK_SIZE;
    size_t total_seq = ((s3_obj_size + chunk_size - 1) / chunk_size);
    while (total_seq > 10000) {
        if (chunk_size < CHUNK_SIZE_MAX) {
            chunk_size *= 2;
        }
        if (chunk_size > CHUNK_SIZE_MAX)
        {
            chunk_size = CHUNK_SIZE_MAX;
        }

        total_seq = ((s3_obj_size + chunk_size - 1) / chunk_size);
    }

    *s3_chunk_sz = chunk_size;
    *s3_seq_total = total_seq;
}

static int ct_archive_data_big (struct hsm_copyaction_private *hcp, const char *src,
                                const char *object_name, int src_fd, struct stat *src_st,
                                const struct hsm_action_item *hai, long hal_flags) {
    struct hsm_extent he;
    time_t last_report_time;
    __u64 file_offset = hai->hai_extent.offset;
    __u64 length = hai->hai_extent.length;
    int rc = 0;
    double start_ct_now = ct_now();
    time_t now;

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
    rc = llapi_hsm_action_progress_ex(hcp, &he, length, 0);
    if (rc < 0) {
        // only log warning for failed to report progress
        tlog_warn("progress ioctl for archive '%s'", object_name);
    }

    // setup properities for put object
    S3PutProperties putProperties;
    ct_mk_put_properties(&putProperties);

    put_object_callback_data data;
    memset(&data, 0, sizeof(put_object_callback_data));

    double	  before_lustre_read = ct_now();
    size_t	  contentLength	     = src_st->st_size;
    size_t	  totalContentLength = src_st->st_size;
    size_t	  todoContentLength  = src_st->st_size;
	size_t    s3_chunk_size;
	size_t    total_seq;

    UploadManager manager;
    manager.upload_id = NULL;
    manager.gb	      = NULL;

    // get multipart upload chunk size and total part number
    ct_get_chunksize(totalContentLength, &s3_chunk_size, &total_seq);

    manager.etags = (char **)calloc(total_seq, sizeof(char *));
    manager.next_etags_pos = 0;

    rc = -EIO;
    int retry_count = RETRYCOUNT;
    bool is_retryable;
    do {
        S3_initiate_multipart(&bucketContext, object_name, 0, &initMultipartHandler,
                              NULL, TIMEOUT_MS, &manager);
        S3Status rc_init = (manager.upload_id == NULL) ? S3StatusErrorInternalError : 0;
        is_retryable = S3_status_is_retryable(rc_init);
    } while (is_retryable && should_retry(&retry_count));

    // TODO: read AWS API DOC, if upload_id always not 0 when where have no error happed
    if (manager.upload_id == NULL) {
        tlog_error( "failed to initiate multipart upload for object '%s' on bucket '%s'",
                    object_name, bucket_name);
        goto clean;
    }

    assert(manager.gb == NULL);

    // prepare file handle for multi part upload
    data.file_name = (char *)src;
    data.fd = src_fd;

    // multi part upload start
    int partContentLength = 0;
    MultipartPartData part_data;
    memset(&part_data, 0, sizeof(MultipartPartData));

    todoContentLength -= s3_chunk_size * manager.next_etags_pos;
    part_data.manager = &manager;
    for (int seq = manager.next_etags_pos + 1; seq <= total_seq; seq++) {
        part_data.seq = seq;
        if (part_data.put_object_data.gb == NULL) {
            part_data.put_object_data = data;
        }
        partContentLength = ((contentLength > s3_chunk_size) ? s3_chunk_size : contentLength);
        tlog_info("%s Part Seq %d, length=%d start", object_name, seq, partContentLength);
        part_data.put_object_data.contentLength = partContentLength;
        part_data.put_object_data.originalContentLength = partContentLength;
        part_data.put_object_data.totalContentLength = todoContentLength;
        part_data.put_object_data.totalOriginalContentLength = totalContentLength;
        putProperties.md5 = 0;
        int retry_count	= RETRYCOUNT;

        do {
            time_t t_begin, t_end;
            double t_cost;

            t_begin = time(NULL);
            // retry must reset file pointer position, because it may have beeen changed
            // in callback function when prepare data for upload
            lseek(data.fd, (seq - 1) * s3_chunk_size, SEEK_SET);
            part_data.put_object_data.status = 0;

            S3_upload_part(&bucketContext, object_name, &putProperties,
                           &uploadMultipartHandler, seq,
                           manager.upload_id, partContentLength, NULL,
                           TIMEOUT_MS, &part_data);

            t_end = time(NULL);

            // when chunk upload success, next_etags_pos will be updated
            // mark status as S3StatusErrorRequestTimeout for retry
            if (manager.next_etags_pos != seq)
            {
                tlog_error("failed to put Part Seq of %d, for object '%s' with rc '%d'",
                            seq, object_name, part_data.put_object_data.status);
                part_data.put_object_data.status = S3StatusErrorRequestTimeout;
            } else {
                // check slow operation
                t_cost = difftime(t_end, t_begin);
                if (t_cost > SLOW_IO_TIME) {
                    tlog_warn("slow put Part Seq of %d, for object '%s' with time '%f' seconds",
                                seq, object_name, t_cost);
                }

                // report progress to HSM coordinator
                if (difftime(t_end, last_report_time) >= ct_opt.o_report_int) {
                    he.offset = (seq - 1) * s3_chunk_size;
                    he.length = partContentLength;
                    tlog_debug("report for archive '%s' progress with offset '%lu' len='%lu'",
                                object_name, he.offset, he.length);
                    int rc_report = llapi_hsm_action_progress_ex(hcp, &he, length, 0);
                    if (rc_report < 0) {
                        // not treat progress report message failed to send as
                        // failure continue process next file part, only log
                        // warning message
                        tlog_warn("progress ioctl for archive '%s' failed with rc=%d",
                                    object_name, rc_report);
                    } else {
                        // update progress
                        last_report_time = time(NULL);
                    }
                }
            }
        } while (S3_status_is_retryable(part_data.put_object_data.status) &&
                 should_retry(&retry_count));

        if (part_data.put_object_data.status != S3StatusOK) {
            tlog_error("failed to put Part Seq of %d, for object '%s'", seq, object_name);
            goto clean;
        }

		tlog_info("%s Part Seq %d, length=%d finish with code %d",
				object_name, seq, partContentLength, part_data.put_object_data.status);
		contentLength -= partContentLength;
		todoContentLength -= partContentLength;
        assert(manager.gb == NULL);
    }

    // multipart upload success, commit it
    int size = 0;
    size += growbuffer_append(&(manager.gb), "<CompleteMultipartUpload>",
                              strlen("<CompleteMultipartUpload>"));

    for (int i = 0, n = 0; i < total_seq; i++) {
        char buf[256];
        n = snprintf(buf, sizeof(buf), "<Part><PartNumber>%d</PartNumber>" \
                     "<ETag>%s</ETag></Part>", i + 1, manager.etags[ i ]);

        size += growbuffer_append(&(manager.gb), buf, n);
    }

    size += growbuffer_append(&(manager.gb), "</CompleteMultipartUpload>",
                              strlen("</CompleteMultipartUpload>"));

    manager.remaining = size;

    retry_count = RETRYCOUNT;
    do {
        S3_complete_multipart_upload(&bucketContext, object_name,
                                     &commitMultipartHandler, manager.upload_id,
                                     manager.remaining, NULL, TIMEOUT_MS,
                                     &manager);

    } while (S3_status_is_retryable(manager.remaining) &&
             should_retry(&retry_count));

    if (manager.remaining) {
        tlog_error("failed to complete multipart upload of %d, for object '%s'",
                   manager.upload_id, object_name);
        goto clean;
    }
	rc = 0;

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
    if (!rc) {
        tlog_info("copied %ju bytes in %f seconds", src_st->st_size, ct_now() - start_ct_now);
    } else {
        tlog_error("failed to upload %s", object_name);
    }

    return rc;
}

static int ct_restore_data(struct hsm_copyaction_private *hcp, const char *src,
                           const char *dst, int dst_fd,
                           const struct hsm_action_item *hai, long hal_flags, char *file_path) {
    double start_ct_now = ct_now();
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
    rc = llapi_hsm_action_progress_ex(hcp, &he, length, 0);
    if (rc < 0) {
        // only log warning for progress message
        tlog_warn("progress ioctl for copy '%s'->'%s' failed", src, dst);
    }

    last_report_time = time(NULL);

    // Downloading from the object store
    char full_path[PATH_MAX];
    sprintf(full_path, "%s/%s", ct_opt.o_mnt, file_path);
    char *object_name = ct_target(full_path);
    if (object_name == NULL)
    {
        tlog_warn("archive file path '%s' not match with config path_prefix '%s'", full_path, path_prefix);
        rc = 0;
        goto out;
    }

    S3GetObjectHandler getObjectHandler = { getResponseHandler,
                                            &get_objectdata_callback };

    if (length == -1) {
        if (file_offset == 0) {
            get_object_callback_data data;
            memset(&data, 0, sizeof(data));
            data.fd = dst_fd;
            data.file_path = file_path;
            rc = get_s3_object(object_name, &data, &getObjectHandler);
            if (rc < 0) {
                goto out;
            }
            length = data.contentLength;
        } else {
            tlog_error("Invalid HSM request");
            assert(0);
        }
    } else {
        tlog_error("Invalid initial value of hai->hai_extent.length %lu", length);
        assert(0);
    }

    now = time(NULL);
    if (now >= last_report_time + ct_opt.o_report_int) {
        tlog_info("sending progress report for archiving %s", src);
        he.offset = file_offset;
        he.length = length;
        rc = llapi_hsm_action_progress_ex(hcp, &he, length, 0);
        if (rc < 0) {
            // only log warning
            tlog_warn("failed to report progress for copy '%s'->'%s'", src, dst);
        }
    }

out:
    tlog_info("copied %jd bytes in %f seconds", (uintmax_t)length,
             ct_now() - start_ct_now);
    return rc;
}

static bool lfs_hsm_archived(char *file_path)
{
    struct hsm_user_state hus;

    int rc = llapi_hsm_state_get(file_path, &hus);
    if (rc) {
        tlog_error("'get HSM state for '%s' failed: %s'", file_path, strerror(-rc));
    } else {
        if ( (hus.hus_states & HS_ARCHIVED) && !(hus.hus_states & HS_DIRTY) )
        {
            tlog_info("'HSM state for '%s' is archived'", file_path);
            return true;
        } else {
            tlog_info("'HSM state for '%s' is not archived'", file_path);
        }
    }

    return false;
}

int ct_archive(const struct hsm_action_item *hai, const long hal_flags, char *file_path) {
    struct hsm_copyaction_private *hcp = NULL;
    char src[PATH_MAX];
    int rc;
    int rcf = 0;
    int hp_flags = 0;
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
        char full_path[PATH_MAX];
        sprintf(full_path, "%s/%s", ct_opt.o_mnt, file_path);
        char *obj_name = ct_target(full_path);
        if (obj_name == NULL) {
            tlog_warn("archive file path '%s' not match with config path_prefix '%s'", full_path, path_prefix);
            rc = 0;
            goto end_ct_archive;
        }

        if (src_st.st_size >= MAX_OBJ_SIZE_LEVEL)
        {
			rc = ct_archive_data_big(hcp, src, obj_name, src_fd, &src_st, hai, hal_flags);
        }
        else
        {
			rc = ct_archive_data(hcp, src, obj_name, src_fd, &src_st, hai, hal_flags);
        }
    }

end_ct_archive:
    err_major++;

    if (rc && ct_is_retryable(rc))
        hp_flags |= HP_FLAG_RETRY;

    rcf = rc;

    if (!(src_fd < 0))
        close(src_fd);

    // use '|', instead of '=', otherwise rc man become 0
    // may be ct_action_done can pass rcf value to rc, but cannot 100% sure from code
    // or can say it cannot (perhaps it because I cannot 100% understand code)
    // lustre/utils/liblustreapi_hsm.c
    int rc_done = ct_action_done(&hcp, hai, hp_flags, rcf);
    if (!rc && rc_done)
    {
        // succes archived file
        // but, failed to call llapi_hsm_action_end
        // but, message may success report to HSM coordinator
        // need check file attribute further
        char full_path[PATH_MAX];
        sprintf(full_path, "%s/%s", ct_opt.o_mnt, file_path);
        if ( lfs_hsm_archived( full_path ) ) {
            rc_done = 0;
        }
    }

    rc |= rc_done;

    return rc;
}

int ct_restore(const struct hsm_action_item *hai, const long hal_flags, char *file_path) {
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

    rc = ct_restore_data(hcp, src, dst, dst_fd, hai, hal_flags, file_path);
    if (rc < 0) {
        tlog_error("cannot restore '%s'", file_path);
        err_major++;
        if (ct_is_retryable(rc))
            hp_flags |= HP_FLAG_RETRY;
        goto end_ct_restore;
    }

end_ct_restore:
    /* object swaping is done by cdt at copy end, so close of volatile file
     * cannot be done before */

    if (!(dst_fd < 0))
        close(dst_fd);

	rc |= ct_action_done(&hcp, hai, hp_flags, rc);

    return rc;
}

int ct_remove(const struct hsm_action_item *hai, const long hal_flags, char *file_path) {
    struct hsm_copyaction_private *hcp = NULL;
    char dst[PATH_MAX];
    int rc;
    int retry_count;
    char *object_name = file_path;

    rc = ct_begin(&hcp, hai);
    if (rc < 0)
        goto end_ct_remove;

    if (ct_opt.o_dry_run) {
        rc = 0;
        goto end_ct_remove;
    }

    retry_count = RETRYCOUNT;
    del_object_callback_data delete_data;

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
    rc |= ct_action_done(&hcp, hai, 0, rc);

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

    tlog_info("copytool cleanup on file system '%s'", ct_opt.o_mnt);
    rc = ct_cleanup();
    if (rc == 0) {
        S3_deinitialize();
    }

    return rc;
}

int main(int argc, char **argv) {
    int rc;
    const int log_size_max = 1024 * 1024 * 50;
    const int log_max = 10;

    // initialize log parameters with multi process write mode
    // because found following message in log:
    // [Auto enable multi-process write mode, log may be lost,
    // please enable multi-process write mode manually]
    unsigned int tlog_flag = 0;
    tlog_flag |= TLOG_MULTI_WRITE;
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

    // set userAgentInfo with "estuary_s3copytool"
    rc = S3_initialize("estuary_s3copytool", S3_INIT_ALL, host);
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

