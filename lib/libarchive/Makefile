LIB=	archive
SRCS= 	archive_check_magic.c \
		archive_entry.c \
		archive_entry_copy_bhfi.c \
		archive_entry_copy_stat.c \
		archive_entry_link_resolver.c \
		archive_entry_stat.c \
		archive_entry_strmode.c \
		archive_entry_xattr.c \
		archive_read.c \
		archive_read_data_into_fd.c \
		archive_read_disk.c \
		archive_read_disk_entry_from_file.c \
		archive_read_disk_set_standard_lookup.c \
		archive_read_extract.c \
		archive_read_open_fd.c \
		archive_read_open_file.c \
		archive_read_open_filename.c \
		archive_read_open_memory.c \
		archive_read_support_compression_all.c \
		archive_read_support_compression_bzip2.c \
		archive_read_support_compression_compress.c \
		archive_read_support_compression_gzip.c \
		archive_read_support_compression_none.c \
		archive_read_support_compression_program.c \
		archive_read_support_compression_uu.c \
		archive_read_support_compression_xz.c \
		archive_read_support_format_all.c \
		archive_read_support_format_ar.c \
		archive_read_support_format_empty.c \
		archive_read_support_format_mtree.c \
		archive_read_support_format_raw.c \
		archive_read_support_format_tar.c \
		archive_read_support_format_xar.c \
		archive_read_support_format_zip.c \
		archive_string.c \
		archive_string_sprintf.c \
		archive_util.c \
		archive_virtual.c \
		archive_write.c \
		archive_write_disk.c \
		archive_write_disk_set_standard_lookup.c \
		archive_write_open_fd.c \
		archive_write_open_file.c \
		archive_write_open_filename.c \
		archive_write_open_memory.c \
		archive_write_set_compression_bzip2.c \
		archive_write_set_compression_compress.c \
		archive_write_set_compression_gzip.c \
		archive_write_set_compression_none.c \
		archive_write_set_compression_program.c \
		archive_write_set_compression_xz.c \
		archive_write_set_format.c \
		archive_write_set_format_ar.c \
		archive_write_set_format_by_name.c \
		archive_write_set_format_mtree.c \
		archive_write_set_format_pax.c \
		archive_write_set_format_shar.c \
		archive_write_set_format_ustar.c \
		archive_write_set_format_zip.c \
		filter_fork.c

.if ${COMPILER_TYPE} == "ack"
SRCS+=  minix_utils.c
.endif

CPPFLAGS+= -DHAVE_CONFIG_H
.if ${NBSD_LIBC} == "yes"
INCSDIR= /usr/include
.else
INCSDIR= /usr/include.ack
.endif
INCS=    archive.h \
		 archive_entry.h

.include <bsd.lib.mk>
