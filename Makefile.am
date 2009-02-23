SUBDIRS = src data
EXTRA_DIST = \
	debian/changelog data/10hildon_welcome \
	debian/compat debian/rules debian/control \
	data/Makefile data/default.conf \
	debian/copyright autogen.sh

deb: dist
	-mkdir $(top_builddir)/debian-build
	cd $(top_builddir)/debian-build && tar zxf ../$(top_builddir)/$(PACKAGE)-$(VERSION).tar.gz
	cd $(top_builddir)/debian-build/$(PACKAGE)-$(VERSION) && dpkg-buildpackage -rfakeroot 

squeaky:
	$(MAKE) distclean
	-rm -rf aclocal.m4 autom*.cache build-stamp config.* configure configure.lineno
	-rm -f depcomp install-sh libtool ltmain.sh Makefile Makefile.in missing stamp-h1
	-rm -f src/Makefile src/Makefile.in src/hildon-welcome data/Makefile.in compile
