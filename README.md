# Estuary

Estuary is a lustre S3 copytool. This means it can interact with the HSM of an existing lustre and send the file contents to S3 via curl. 

This copytool is an updated version of the [lustre-s3-copytool](https://github.com/ComputeCanada/lustre-obj-copytool) by ComputeCanada.

ng ---- only a postfix to difference with none "-ng" version

# Features

change from none "-ng" version

remove LZ4 support

bucket and file system path mapping 1:1

remove checksum when restore

# Building

Estuary requires the following development packages:

```sh
sudo dnf install -y libcurl-devel libxml2-devel openssl-devel libconfig-devel libbsd-devel
```

and Lustre development headers, see the [LustreSetupGuide](./docs/LustreSetupGuide.md) for some info.

A recent version of CMake (`>3.24`) is also required (this is newer than in most distros), there is [a script here](./infra/scripts/bootstrap_cmake.sh) to fetch it if needed.

Then do:

```sh
git clone https://git.ichec.ie/performance/storage/estuary.git
mkdir build; cd build
cmake ../estuary
make
```

the binary `estuary_s3copytool` will be in the `bin` directory of `build`.

# Running

## Command line arguments

To see the full list of command line arguments run `./estuary_s3copytool --help`:

```sh
# ./estuary_s3copytool --help
 Usage: estuary_s3copytool [options]... <mode> <lustre_mount_point>
The Lustre HSM S3 copy tool can be used as a daemon or as a command line tool
The Lustre HSM daemon acts on action requests from Lustre
to copy files to and from an HSM archive system.
   --daemon            Daemon mode, run in background
 Options:
The Lustre HSM tool performs administrator-type actions
on a Lustre HSM archive.
   --abort-on-error          Abort operation on major error
   -A, --archive <#>         Archive number (repeatable)
   -c, --config <path>       Path to the config file
   --dry-run                 Don't run, just show what would be done
   -q, --quiet               Produce less verbose output
   -u, --update-interval <s> Interval between progress reports sent
                             to Coordinator
   -v, --verbose             Produce more verbose output
```

## Config file

Copy the file [config.cfg](./config.cfg) to your runtime directory and fill it out as required (the path of the config file can also be passed as a runtime parameter).

| Parameter | Type | Description |
|-----------|------|-------------|
| access_key | String | AWS access key. |
| secret_key | String | AWS Secret key. |
| host | String | Hostname of the S3 endpoint. |
| bucket_count | Int | The number of buckets used to spread the indexing load. With radosgw, PUT operation will slow down proportionally to the number of objects in the same bucket. If a bucket_count > 2 is used, the bucket_prefix will be appended an ID. |
| bucket_prefix | String | This prefix will prepended to each bucketID. For example, if the bucket_prefix is `hsm`, then each bucket will named `hsm_0`, `hsm_1`, `hsm_2` ... |
| ssl | Bool | If the S3 endpoint should use SSL. |
| chunk_size | Int | This represent the size of the largest object stored. A large file in Lustre will be stripped in multiple objects if the file size > chunk_size. Because compression is used, this parameter need to be set according to the available memory. Each thread will use twice the chunk_size. For incompressible data, each object will take a few extra bytes. |

If you want a local S3 test server there are notes in the [Developer Guide](./docs/DeveloperGuide.md) for using Minio.

## Lustre HSM

Enable HSM on the MDS server

```sh
# lctl set_param mdt.lustre-MDT0000.hsm.max_requests=10
# lctl set_param mdt.lustre-MDT0000.hsm_control=enabled
```

Start the copytool on a DTN node

```sh
# ./estuary_s3copytool /lustre/
1456506103.926649 copytool_d[31507]: mount_point=/lustre/
1456506103.932785 copytool_d[31507]: waiting for message from kernel
```

You can use `lfs hsm_state` to get the current status of a file

Move a file to HSM and remove it from Lustre

```sh
# lfs hsm_state test
test: (0x00000000)
# lfs hsm_archive test
# lfs hsm_state
test: (0x00000009) exists archived, archive_id:1
# lfs hsm_release test
# lfs hsm_state test
test: (0x0000000d) released exists archived, archive_id:1
```

Restore the file implicitly

```sh
# md5sum test
33e3e3bdb7f6f847e06ae2a8abad0b85  test
# lfs hsm_state test
test: (0x00000009) exists archived, archive_id:1
```

Remove the file from S3

```sh
# lfs hsm_remove test
# lfs hsm_state test
test: (0x00000000), archive_id:1
```
