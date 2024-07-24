#!/bin/sh

yum --nogpgcheck --disablerepo=* --enablerepo=e2fsprogs-wc \
    install -y e2fsprogs

yum --nogpgcheck --disablerepo=base,extras,updates \
    --enablerepo=lustre-server install -y \
    kernel \
    kernel-devel \
    kernel-headers \
    kernel-tools \
    kernel-tools-libs \
    kernel-tools-libs-devel
