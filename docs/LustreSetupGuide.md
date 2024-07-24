This guide covers setting up a Lustre build and test environment.

# Machine Setup

Lustre needs to be run on a Virtual Machine rather than a container due to its interactions with the kernel. We use a Rocky 8 minimal server install as a basis.

Due to the use of kernel modules when selecting a RHEL/Rocky version to download you should check that it is supported by an obtainable Lustre version, for example RHEL 8.8 is supported by Lustre 2.15.3 and up: https://wiki.whamcloud.com/display/PUB/Lustre+Support+Matrix.

You should grab an older ISO from the archives if the latest isn't supported by Lustre yet.

# Set up Lustre repos

This section follows from: https://wiki.lustre.org/Installing_the_Lustre_Software, with VM specific details here: https://wiki.lustre.org/KVM_Quick_Start_Guide

On the VM we will point dnf to the following repos, you may need to check the `el` version for something suitable for your install. Save the below:

```ini
[lustre-server]
name=lustre-server
baseurl=https://downloads.whamcloud.com/public/lustre/latest-release/el8.8/server
# exclude=*debuginfo*
gpgcheck=0

[lustre-client]
name=lustre-client
baseurl=https://downloads.whamcloud.com/public/lustre/latest-release/el8.8/client
# exclude=*debuginfo*
gpgcheck=0

[e2fsprogs-wc]
name=e2fsprogs-wc
baseurl=https://downloads.whamcloud.com/public/e2fsprogs/latest/el8
# exclude=*debuginfo*
gpgcheck=0
```

to `/etc/yum.repos.d/lustre.repo` and then run:

```sh
sudo dnf install epel-release
sudo dnf config-manager --set-enabled powertools
sudo dnf update
```

To enable using DKMS for kernal modules install the following:

```sh
sudo dnf install \
asciidoc audit-libs-devel automake bc \
binutils-devel bison elfutils-devel \
elfutils-libelf-devel expect flex gcc gcc-c++ git \
glib2 glib2-devel hmaccalc keyutils-libs-devel krb5-devel ksh \
libattr-devel libblkid-devel libselinux-devel libtool \
libuuid-devel lsscsi make ncurses-devel \
net-snmp-devel net-tools newt-devel numactl-devel \
parted patchutils pciutils-devel perl-ExtUtils-Embed \
pesign python3-devel redhat-rpm-config rpm-build systemd-devel \
tcl tcl-devel tk tk-devel wget xmlto yum-utils zlib-devel
```

If any packages are expired or missing just try removing them from the list.

Reboot and install the following:

```sh
sudo dnf install --nogpgcheck --enablerepo=lustre-server install \
kmod-lustre \
kmod-lustre-osd-ldiskfs \
lustre-osd-ldiskfs-mount \
lustre \
lustre-resource-agents
```

We will install the client also:

```sh
sudo dnf --nogpgcheck --enablerepo=lustre-client install \
lustre-client-dkms \
lustre-client
```

You can ignore errors with conflicts.

# Running the test client

You can clone the cluster repo (this is a handy mirror: https://github.com/hpc/lustre/tree/master)

Before running the test client you need to make sure that the hostname for the machine does not resolve to a loopback device - this is likely to be the case with a default VM setup.

Get the ip assigned by the VM tool, e.g. with `ifconfig` and in `/etc/hosts` if there is a `localhost.localdomain` entry under `127.0.0.1` or `::1` then remove it and give it its own entry under the ip you found with `ifconfig`.

Now you can run the script `lustre/tests/llmount.sh` to run the test system and `llmountcleanep.sh` to clean up after.