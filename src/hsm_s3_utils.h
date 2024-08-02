#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>

#include "ct_common.h"
#include "growbuffer.h"
#include "libs3.h"
#include "s3_callback.h"

typedef struct UploadManager {
    // used for initial multipart
    char *upload_id;

    // used for upload part object
    char **etags;
    int next_etags_pos;

    // used for commit Upload
    growbuffer *gb;
    int remaining;
} UploadManager;

typedef struct MultipartPartData {
    put_object_callback_data put_object_data;
    int seq;
    UploadManager *manager;
} MultipartPartData;

static S3ResponseHandler getResponseHandler = {
                         &s3_response_get_object_properties_callback,
                         &s3_response_complete_callback };

static S3ResponseHandler putResponseHandler = {
                         &s3_response_properties_callback,
                         &s3_response_complete_callback };

static S3ResponseHandler headResponseHandler = {
                         &s3_response_properties_callback,
                         &s3_response_complete_callback };

static S3ResponseHandler deleteResponseHandler = {
                         &s3_response_properties_callback,
                         &s3_response_complete_callback };

static S3MultipartInitialHandler initMultipartHandler = {
    {
        // will be called when multipart upload initialize success
        &s3_response_properties_callback,

        // always called regardless multipart upload initialize success or not
        &multipart_response_complete_callback
    },

    // after initialize of a multipart upload operation, it will be called,
    // it may called even when failed to initialize, so must check upload_id
    // when failed in initialize (such failed to connect to S3 endpoint), upload_id
    // will be empty string, must check in this function, instead of strdup directly
    // otherwize, it will lead multipart work logic cannot work correctly
    &initial_multipart_response_callback
};

static S3PutObjectHandler uploadMultipartHandler = {
    {
        // will be called when part of object put success
        &multipart_put_part_response_properies_callback,

        // always called regardless part of object put success or not
        &multipart_response_complete_callback
    },
    // S3_upload_part --> request_perform --> easy_transfer --> curl_multi_perform
    // --> multi_runsingle --> Curl_readwrite --> readwrite_upload --> Curl_fillreadbuffer
    // --> curl_read_func --> multipart_put_objectdata_callback
    // this one will called first in upload process, it responsible for prepare data for upload
    &multipart_put_objectdata_callback
};

static S3MultipartCommitHandler commitMultipartHandler = {
    // the first will be called when client send request to S3 endpoint
    // then, the 2nd wil be called
    {
        // S3_complete_multipart_upload --> request_perform --> easy_transfer --> curl_multi_perform
        // multi_runsingle --> Curl_readwrite --> readwrite_data --> Curl_client_write --> chop_write
        // curl_write_func --> request_headers_done --> commitMultipartPropertiesCallback -->
        // s3_response_properties_callback
        &s3_response_properties_callback,
        // S3_complete_multipart_upload --> request_perform --> request_finish -->
        // commitMultipartCompleteCallback --> multipart_response_complete_callback
        &multipart_response_complete_callback
    },
    // S3_complete_multipart_upload --> request_perform --> curl_easy_perform --> easy_perform
    // -->easy_transfer --> curl_multi_perform --> multi_runsingle --> readwrite_data
    // --> Curl_client_write --> chop_write --> curl_write_func --> request_headers_done
    // --> commitMultipartPropertiesCallback --> multipart_response_complete_callback
    &multipart_commit_response_callback,
    NULL
};

