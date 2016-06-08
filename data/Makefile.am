SUBDIRS = icons

xchat_gnome_DATA = 			\
	channel-list.glade		\
	connect-dialog.glade		\
	dcc-window.glade		\
	irc-network-editor.glade	\
	preferences-dialog.glade	\
	setup-dialog.glade		\
	topic-change.glade		\
	xchat-gnome-ui.xml		\
	xchat-gnome.glade

xchat_gnomedir = $(datadir)/xchat-gnome

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

#
# Desktop file
#

desktopdir = $(datadir)/applications
desktop_in_files = xchat-gnome.desktop.in.in
desktop_files = $(desktop_in_files:.desktop.in.in=.desktop)
desktop_DATA = $(desktop_files)
@INTLTOOL_DESKTOP_RULE@

#
# Man page
#
man_MANS = xchat-gnome.1

DISTCLEANFILES =			\
	$(desktop_files)

EXTRA_DIST = 				\
	$(desktop_in_files)		\
	$(xchat_gnome_DATA)		\
	$(icon_DATA)			\
	$(man_MANS)
