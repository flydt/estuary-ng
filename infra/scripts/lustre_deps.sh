#!/bin/sh

yum -y groupinstall "Development Tools"

yum -y install xmlto asciidoc elfutils-libelf-devel zlib-devel binutils-devel \
    newt-devel python-devel hmaccalc perl-ExtUtils-Embed bison elfutils-devel \
    audit-libs-devel libattr-devel libuuid-devel libblkid-devel libselinux-devel \
    libudev-devel 
yum -y install epel-release
yum -y install pesign numactl-devel pciutils-devel ncurses-devel libselinux-devel \
    fio openssl which libyaml-devel wget e2fsprogs vim openssl-devel keyutils
