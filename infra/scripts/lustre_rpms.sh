#!/bin/sh

cat >/tmp/lustre-repo.conf <<\__EOF
[lustre-server]
name=lustre-server
baseurl=https://downloads.whamcloud.com/public/lustre/latest-release/el7/server
# exclude=*debuginfo*
gpgcheck=0

[lustre-client]
name=lustre-client
baseurl=https://downloads.whamcloud.com/public/lustre/latest-release/el7/client
# exclude=*debuginfo*
gpgcheck=0

[e2fsprogs-wc]
name=e2fsprogs-wc
baseurl=https://downloads.whamcloud.com/public/e2fsprogs/latest/el7
# exclude=*debuginfo*
gpgcheck=0
__EOF

yum -y install yum-utils createrepo

mkdir -p /var/www/html/repo
cd /var/www/html/repo
reposync -c /tmp/lustre-repo.conf -n \
    -r lustre-server \
    -r lustre-client \
    -r e2fsprogs-wc

cd /var/www/html/repo
for i in e2fsprogs-wc lustre-client lustre-server; do
    (cd $i && createrepo .)
done

hn=`hostname --fqdn`
cat >/var/www/html/lustre.repo <<__EOF
[lustre-server]
name=lustre-server
baseurl=file:///var/www/html/repo/lustre-server
enabled=1
priority=1
gpgcheck=0
proxy=_none_

[lustre-client]
name=lustre-client
baseurl=file:///var/www/html/repo/lustre-client
enabled=1
priority=1
gpgcheck=0

[e2fsprogs-wc]
name=e2fsprogs-wc
baseurl=file:///var/www/html/repo/e2fsprogs-wc
enabled=1
gpgcheck=0
priority=1
__EOF

cp /var/www/html/lustre.repo /etc/yum.repos.d/


