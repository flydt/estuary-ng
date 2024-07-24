FROM rockylinux:8

RUN yum check-update -q; yum update -qy; yum -y groupinstall "Development Tools";

RUN yum config-manager --set-enabled powertools; dnf install -y gcc autoconf libtool which make patch diffutils file binutils-devel python38 python3-devel elfutils-devel libselinux-devel libaio-devel dnf-plugins-core bc bison flex git libyaml-devel libnl3-devel libmount-devel json-c-devel redhat-lsb libssh-devel libattr-devel libtirpc-devel libblkid-devel openssl-devel libuuid-devel texinfo texinfo-tex
RUN yum -y install audit-libs-devel binutils-devel elfutils-devel kabi-dw ncurses-devel newt-devel numactl-devel openssl-devel pciutils-devel perl perl-devel python2 python3-docutils xmlto xz-devel elfutils-libelf-devel libcap-devel libcap-ng-devel llvm-toolset libyaml libyaml-devel kernel-rpm-macros kernel-abi-whitelists
RUN dnf install -y epel-release ccache bpftool dwarves java-devel libbabeltrace-devel libbpf-devel libmnl-devel net-tools rsync

RUN git clone "https://review.whamcloud.com/tools/e2fsprogs" e2fsprogs && cd e2fsprogs && git checkout v1.47.0-wc1 && ./configure --with-root-prefix=/usr --enable-elf-shlibs --disable-uuidd --disable-fsck --disable-e2initrd-helper --disable-libblkid --disable-libuuid --enable-quota --disable-fuse2fs && make -j8
RUN make install && cd ..