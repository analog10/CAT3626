DESCRIPTION = "CAT3626 device"
SECTION = "kernel/modules"
PRIORITY = "optional"
LICENSE = "GPL"
PR = "r1"

S = "${WORKDIR}"

SRC_URI = "file://cat3626.c \
          file://Makefile "

inherit module

do_compile () {
        unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS CC LD CPP
        oe_runmake 'MODPATH={D}${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/' \
                   'KERNEL_SOURCE=${STAGING_KERNEL_DIR}' \
                   'KDIR=${STAGING_KERNEL_DIR}' \
                   'KERNEL_VERSION=${KERNEL_VERSION}' \
                   'CC=${KERNEL_CC}' \
                   'LD=${KERNEL_LD}'
}

do_install() {
        install -d ${D}${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/
        install -m 0644 ${S}/cat3626*${KERNEL_OBJECT_SUFFIX} ${D}${base_libdir}/modules/${KERNEL_VERSION}/kernel/drivers/
}
