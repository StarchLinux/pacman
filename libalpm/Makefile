LIB = alpm

CFLAGS += -include ../config.h -D_GNU_SOURCE

HDR = add.h alpm.h backup.h base64.h conflict.h db.h delta.h deps.h diskspace.h dload.h filelist.h graph.h group.h handle.h log.h package.h pkghash.h remove.h sync.h trans.h util.h

SRCS = \
        add.c \
        alpm.c \
        alpm_list.c \
        backup.c \
        base64.h base64.c \
        be_local.c \
        be_package.c \
        be_sync.c \
        conflict.c \
        db.c \
        delta.c \
        deps.c \
        diskspace.c \
        dload.c \
        error.c \
        filelist.c \
        graph.c \
        group.c \
        handle.c \
        log.c \
        package.c \
        pkghash.c \
        rawstr.c \
        remove.c \
        signing.c \
        sync.c \
        trans.c \
        util.c \
        version.c

LDADD += -larchive -lcurl -lz

.ifndef HAVE_LIBSSL
SRCS += \
	md5.h md5.c \
	sha2.h sha2.c

LDADD += -lssl
.endif

.include <bsd.lib.mk>
.include <bsd.hdr.mk>

install: hdrinstall
