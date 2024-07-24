# Testing with Minio

You can test with an S3 backend like Minio https://min.io/download#/linux

```sh
dnf install https://dl.min.io/server/minio/release/linux-amd64/minio-20230923034750.0.0.x86_64.rpm
MINIO_ROOT_USER=admin MINIO_ROOT_PASSWORD=password minio server $MY_MINIO_CACHE --console-address ":9001"
wget https://dl.min.io/client/mc/release/linux-amd64/mc
chmod +x mc
./mc alias set myminio/ http://127.0.0.1:9000 admin password
```

Set the endpoint url, user name and password in the config.cfg file for the copytool.

You can create a bucket with:

```sh
./mc mb myminio/lustr-hsm-1
```

and list buckets and objects with:

```sh
./mc ls myminio
```