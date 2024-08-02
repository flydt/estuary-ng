#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>

#include "libs3.h"
#include "growbuffer.h"
#include "ct_common.h"

typedef struct put_object_callback_data {
    size_t buffer_offset;
    S3Status status;
    char *buffer;
    FILE *infile;
    growbuffer *gb;
    size_t contentLength;
    size_t originalContentLength;
    size_t totalContentLength;
    size_t totalOriginalContentLength;
    int noStatus;
} put_object_callback_data;

typedef struct get_object_callback_data {
    size_t buffer_offset;
    size_t totalLength;
    size_t contentLength;
    char *buffer;
    S3Status status;
    char md5[ MD5_ASCII ];
} get_object_callback_data;

int put_objectdata_callback(int bufferSize, char *buffer, void *callbackData);

S3Status get_objectdata_callback(int bufferSize, const char *buffer,
                                 void *callbackData);

S3Status s3_response_properties_callback(const S3ResponseProperties *properties,
                                         void *callbackData);

S3Status s3_response_get_object_properties_callback(const S3ResponseProperties *properties,
                                                    void *callbackData);

void s3_response_complete_callback(S3Status status, const S3ErrorDetails *error,
                                   void *callbackData);

S3Status initial_multipart_response_callback(const char * upload_id,
                                             void * callbackData);

S3Status multipart_put_part_response_properies_callback(const S3ResponseProperties *properties,
                                                        void *callbackData);

int multipart_commit_response_callback(int bufferSize, char *buffer, void *callbackData);

void multipart_response_complete_callback(S3Status status, const S3ErrorDetails *error,
                                          void *callbackData);

int multipart_put_objectdata_callback(int bufferSize, char *buffer, void *callbackData);
