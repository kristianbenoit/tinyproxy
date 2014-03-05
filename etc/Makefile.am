pkgsysconfdir = $(sysconfdir)/$(PACKAGE)

pkgsysconf_DATA = \
	tinyproxy.conf

if SNREPLACE_ENABLE
pkgsysconf_DATA += snreplace
endif


EXTRA_DIST = \
	tinyproxy.conf.in \
	snreplace

edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@datadir[@]|$(datadir)|g' \
	-e 's|@datarootdir[@]|$(datarootdir)|g' \
	-e 's|@pkgsysconfdir[@]|$(pkgsysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@pkgdatadir[@]|$(pkgdatadir)|g' \
	-e 's|@prefix[@]|$(prefix)|g' \
	-e 's|@TINYPROXY_STATHOST[@]|$(TINYPROXY_STATHOST)|g'

tinyproxy.conf: $(top_srcdir)/etc/tinyproxy.conf.in Makefile
	@rm -f $@ $@.tmp
	$(AM_V_GEN) $(edit) $(top_srcdir)/etc/$@.in > $@.tmp
	@mv $@.tmp $@

CLEANFILES = \
	tinyproxy.conf
