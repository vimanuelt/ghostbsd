# $FreeBSD$

PACKAGE=runtime
PROG=	start-stop-daemon

.include <src.opts.mk>

SDSRC=    ${SRCTOP}/contrib/openrc/src

.PATH:  ${SDSRC}/rc

SRCS=	start-stop-daemon.c \
		rc-misc.c \
		rc-schedules.c \
		rc-pipes.c \
		_usage.c
CFLAGS+=	-I${SDSRC}/rc \
		-I${SDSRC}/libeinfo \
		-I${SDSRC}/librc \
                -I${.OBJDIR}/../../lib/librc/contrib/openrc/src/librc \
		-I${SDSRC}/includes

WARNS?=	3
MAN=	${.CURDIR}/../../contrib/openrc/man/start-stop-daemon.8

LIBADD=	einfo rc util

.include <bsd.prog.mk>
