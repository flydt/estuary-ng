#!/bin/sh

yum --nogpgcheck --enablerepo=lustre-server install -y \
    kmod-lustre \
    kmod-lustre-osd-ldiskfs \
    kmod-lustre-tests \
    lustre-tests \
    lustre-osd-ldiskfs-mount \
    lustre \
    lustre-resource-agents

