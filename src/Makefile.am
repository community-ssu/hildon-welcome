bin_PROGRAMS = hildon-welcome

hildon_welcome_SOURCES = \
	main.c \
	conffile.c conffile.h \
	$(NULL)

hildon_welcome_CFLAGS = \
	$(HILDON_WELCOME_DEPS_CFLAGS) \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DDATADIR=\"$(datadir)\" \
	$(NULL)

hildon_welcome_LDADD = \
	$(HILDON_WELCOME_DEPS_LIBS) \
	$(NULL)
