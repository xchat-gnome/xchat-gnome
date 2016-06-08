SUBDIRS = data help plugins po src

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST =			\
	autogen.sh		\
	ChangeLog.pre-git

DISTCLEANFILES =	

DISTCHECK_CONFIGURE_FLAGS = \
	--disable-scrollkeeper	\
	--disable-schemas-install

# Build ChangeLog from GIT  history
ChangeLog:
	@if test -d $(top_srcdir)/.git; then \
		GIT_DIR="$(top_srcdir)/.git" git log --stat > $@; \
	fi

dist: ChangeLog

.PHONY: ChangeLog
