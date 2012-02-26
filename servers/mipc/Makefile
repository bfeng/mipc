# Makefile for Data Store Server (DS)
PROG=	mipc
SRCS=	main.c

DPADD+=	${LIBSYS}
LDADD+=	-lsys

MAN=

BINDIR?= /usr/sbin
INSTALLFLAGS+=	-S 16k

.include <minix.bootprog.mk>
