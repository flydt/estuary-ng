#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "hsm_s3_utils.h"
#include "tlog.h"

///////////////////////////////////////////////////////////////////////////////////////
// all of callback function define
///////////////////////////////////////////////////////////////////////////////////////

int putObjectDataCallback(int bufferSize, char *buffer,
                                 void *callbackData) {
    put_object_callback_data *data = (put_object_callback_data *)callbackData;
    int size = 0;
    assert(data && buffer);

    if (data->contentLength) {
        if (data->contentLength > bufferSize) {
            // Limited by bufferSize
            size = bufferSize;
        } else {
            // Last chunk
            size = data->contentLength;
        }
        memcpy(buffer, data->buffer + data->buffer_offset, size);
        data->buffer_offset += size;
    }

    data->contentLength -= size;
    return size;
}

void getResponseCompleteCallback(S3Status status,
                                        const S3ErrorDetails *error,
                                        void *callbackData) {
    get_object_callback_data *data = (get_object_callback_data *)callbackData;
    data->status = status;
    return;
}

void putResponseCompleteCallback(S3Status status,
                                        const S3ErrorDetails *error,
                                        void *callbackData) {
    put_object_callback_data *data = (put_object_callback_data *)callbackData;
    data->status = status;
    return;
}

S3Status responseGetObjectPropertiesCallback(const S3ResponseProperties *properties,
                                    void *callbackData) {
    get_object_callback_data *data = (get_object_callback_data *)callbackData;

    assert(data && properties);
    tlog_info("properties info1: contentLength %ld, eTag:%s, metaDataCount:%ld",
              properties->contentLength, properties->eTag, properties->metaDataCount);
    data->contentLength = properties->contentLength;

    if (properties->eTag) {
        strncpy(data->md5, properties->eTag + 1, MD5_ASCII - 1);
    }

    for (int i = 0; i < properties->metaDataCount; i++) {
        if (strcmp(properties->metaData[i].name, "chunksize") == 0) {
            char *err;
            data->chunk_size = strtoll(properties->metaData[i].value, &err, 10);
            tlog_info("properties info2: i %d, value:%ld, chunk_size:%ld",
                      i, properties->metaData[i].value, strtoll(properties->metaData[i].value, &err, 10));
            if (*err) {
                tlog_error("Error while parsing chunk_size, non-covertible part: %s", err);
                return S3StatusAbortedByCallback;
            }
        } else if (strcmp(properties->metaData[i].name, "totallength") == 0) {
            char *err;
            data->totalLength = strtoll(properties->metaData[i].value, &err, 10);
            tlog_info("properties info3: i %d, value:%ld, totalLength:%ld",
                      i, properties->metaData[i].value, strtoll(properties->metaData[i].value, &err, 10));
            if (*err) {
                tlog_error("Error while parsing totalLength, non-covertible part: %s", err);
                return S3StatusAbortedByCallback;
            }
        }
    }

    tlog_info("data info1: buffer_offset %ld, chunk_size:%ld, totalLength:%ld, contentLength:%ld, md5:%s",
              data->buffer_offset, data->chunk_size, data->totalLength, data->contentLength, data->md5);
    return S3StatusOK;
}

S3Status getObjectDataCallback(int bufferSize, const char *buffer, void *callbackData) {
    get_object_callback_data *data = (get_object_callback_data *)callbackData;
    int size = 0;

    assert(data && buffer);
    if (data->contentLength) {
        if (data->buffer == NULL) {
            // Allocate memory for the buffer
            data->buffer = malloc(data->contentLength);
            if (data->buffer == NULL) {
                // Allocation error
                return S3StatusAbortedByCallback;
            }
        }

        if (data->contentLength > bufferSize) {
            // Limited by bufferSize
            size = bufferSize;
        } else {
            // Last chunk
            size = data->contentLength;
        }
        memcpy(data->buffer + data->buffer_offset, buffer, size);
        data->buffer_offset += size;
    }
    return ((size < (size_t)bufferSize) ? S3StatusAbortedByCallback
                                        : S3StatusOK);
}

S3Status initial_multipart_response_callback(const char * upload_id,
                                    void * callbackData)
{
    UploadManager *manager = (UploadManager *) callbackData;

    // when failed to initial multipart, the callback function still called
    // but without upload_id, we use "upload_id == NULL" as failed to initialize
    // so, don't set value in this result
    if (strlen(upload_id))
    {
        manager->upload_id = strdup(upload_id);
    }

    return S3StatusOK;
}

S3Status multipart_put_part_response_properies_callback
    (const S3ResponseProperties *properties, void *callbackData)
{
    responsePropertiesCallback(properties, callbackData);
    MultipartPartData *data = (MultipartPartData *) callbackData;
    int seq = data->seq;
    const char *etag = properties->eTag;
    data->manager->etags[seq - 1] = strdup(etag);
    data->manager->next_etags_pos = seq;
    tlog_info("callback for put_part seq of %d,", seq);
    return S3StatusOK;
}


int multipart_put_objectdata_callback(int bufferSize, char *buffer,
                                 void *callbackData)
{
    put_object_callback_data *data =
        (put_object_callback_data *) callbackData;

    int ret = 0;
    if (data->contentLength) {
        int toRead = ((data->contentLength > (unsigned) bufferSize) ?
                      (unsigned) bufferSize : data->contentLength);
        if (data->infile) {
            tlog_info("read data by 'fread' in offset %ld", ftell(data->infile));
            ret = fread(buffer, 1, toRead, data->infile);
        } else {
            tlog_error("infile not initialized");
            abort();
        }
    }

    data->contentLength -= ret;
    data->totalContentLength -= ret;

    if (data->contentLength && !data->noStatus) {
        size_t data_remain = data->totalOriginalContentLength - data->totalContentLength;
        size_t ratio_remain = (100 * data_remain) / data->totalOriginalContentLength;

        tlog_info("%llu bytes remaining (%d%% complete) ...", data->totalContentLength, ratio_remain);
    }

    return ret;
}

int multipart_commit_response_callback(int bufferSize, char *buffer, void *callbackData)
{
    UploadManager *manager = (UploadManager*)callbackData;
    int ret = 0;
    if (manager->remaining) {
        int toRead = ((manager->remaining > bufferSize) ?
                      bufferSize : manager->remaining);
        growbuffer_read(&(manager->gb), toRead, &ret, buffer);
    }
    manager->remaining -= ret;
    return ret;
}

// response complete callback ------------------------------------------------
void multipart_response_complete_callback(S3Status status,
                                     const S3ErrorDetails *error,
                                     void *callbackData)
{
    assert(error);
    (void) callbackData;
    if (error && error->message) {
        tlog_error("Message: %s", error->message);
    }
    if (error && error->resource) {
        tlog_error("Resource: %s", error->resource);
    }
    if (error && error->furtherDetails) {
        tlog_error("Further Details: %s", error->furtherDetails);
    }
    if (error && error->extraDetailsCount) {
        tlog_error("Extra Details:");
        for (int i = 0; i < error->extraDetailsCount; i++) {
                   tlog_error("%s: %s", error->extraDetails[i].name, error->extraDetails[i].value);
        }
    }
}

// This callback does the same thing for every request type: prints out the
// properties if the user has requested them to be so
S3Status responsePropertiesCallback
    (const S3ResponseProperties *properties, void *callbackData)
{
    (void) callbackData;
#ifdef DEBUG
    if (properties->contentType)
        tlog_info("Content-Type: %s", properties->contentType);
    if (properties->requestId)
        tlog_info("Request-Id: %s", properties->requestId);
    if (properties->requestId2)
        tlog_info("Request-Id-2: %s", properties->requestId2);
    if (properties->contentLength > 0)
        tlog_info("Content-Length: %llu", properties->contentLength);
    if (properties->server)
        tlog_info("Server: %s", properties->server);
    if (properties->eTag > 0)
        tlog_info("ETag: %s", properties->eTag);

    if (properties->lastModified > 0) {
        char timebuf[256];
        time_t t = (time_t) properties->lastModified;
        struct tm tm_ret;
        gmtime_r(&t, &tm_ret);
        // gmtime is not thread-safe but we don't care here.
        // strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
        tlog_info("Last-Modified: %s", timebuf);
    }
    for (int i = 0; i < properties->metaDataCount; i++) {
        tlog_info("x-amz-meta-%s: %s", properties->metaData[i].name,
               properties->metaData[i].value);
    }
    if (properties->usesServerSideEncryption) {
        tlog_info("UsesServerSideEncryption: true");
    }
#endif

    return S3StatusOK;
}