/*
 *  package.c
 *
 *  Copyright (c) 2006-2012 Pacman Development Team <pacman-dev@archlinux.org>
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <time.h>

#include <alpm.h>
#include <alpm_list.h>

/* pacman */
#include "package.h"
#include "util.h"
#include "conf.h"

#define CLBUF_SIZE 4096

/** Turn a depends list into a text list.
 * @param deps a list with items of type alpm_depend_t
 */
static void deplist_display(const char *title,
		alpm_list_t *deps, unsigned short cols)
{
	alpm_list_t *i, *text = NULL;
	for(i = deps; i; i = alpm_list_next(i)) {
		alpm_depend_t *dep = i->data;
		text = alpm_list_add(text, alpm_dep_compute_string(dep));
	}
	list_display(title, text, cols);
	FREELIST(text);
}

/** Turn a optdepends list into a text list.
 * @param optdeps a list with items of type alpm_optdepend_t
 */
static void optdeplist_display(const char *title,
		alpm_list_t *optdeps, unsigned short cols)
{
	alpm_list_t *i, *text = NULL;
	for(i = optdeps; i; i = alpm_list_next(i)) {
		alpm_depend_t *optdep = i->data;
		text = alpm_list_add(text, alpm_dep_compute_string(optdep));
	}
	list_display_linebreak(title, text, cols);
	FREELIST(text);
}

/**
 * Display the details of a package.
 * Extra information entails 'required by' info for sync packages and backup
 * files info for local packages.
 * @param pkg package to display information for
 * @param from the type of package we are dealing with
 * @param extra should we show extra information
 */
void dump_pkg_full(alpm_pkg_t *pkg, int extra)
{
	unsigned short cols;
	time_t bdate, idate;
	alpm_pkgfrom_t from;
	double size;
	char bdatestr[50] = "", idatestr[50] = "";
	const char *label, *reason;
	alpm_list_t *validation = NULL, *requiredby = NULL;

	from = alpm_pkg_get_origin(pkg);

	/* set variables here, do all output below */
	bdate = (time_t)alpm_pkg_get_builddate(pkg);
	if(bdate) {
		strftime(bdatestr, 50, "%c", localtime(&bdate));
	}
	idate = (time_t)alpm_pkg_get_installdate(pkg);
	if(idate) {
		strftime(idatestr, 50, "%c", localtime(&idate));
	}

	switch(alpm_pkg_get_reason(pkg)) {
		case ALPM_PKG_REASON_EXPLICIT:
			reason = _("Explicitly installed");
			break;
		case ALPM_PKG_REASON_DEPEND:
			reason = _("Installed as a dependency for another package");
			break;
		default:
			reason = _("Unknown");
			break;
	}

	alpm_pkgvalidation_t v = alpm_pkg_get_validation(pkg);
	if(v) {
		if(v & ALPM_PKG_VALIDATION_NONE) {
			validation = alpm_list_add(validation, _("None"));
		} else {
			if(v & ALPM_PKG_VALIDATION_MD5SUM) {
				validation = alpm_list_add(validation, _("MD5 Sum"));
			}
			if(v & ALPM_PKG_VALIDATION_SHA256SUM) {
				validation = alpm_list_add(validation, _("SHA256 Sum"));
			}
			if(v & ALPM_PKG_VALIDATION_SIGNATURE) {
				validation = alpm_list_add(validation, _("Signature"));
			}
		}
	} else {
		validation = alpm_list_add(validation, _("Unknown"));
	}

	if(extra || from == ALPM_PKG_FROM_LOCALDB) {
		/* compute this here so we don't get a pause in the middle of output */
		requiredby = alpm_pkg_compute_requiredby(pkg);
	}

	cols = getcols(fileno(stdout));

	/* actual output */
	if(from == ALPM_PKG_FROM_SYNCDB) {
		string_display(_("Repository     :"),
				alpm_db_get_name(alpm_pkg_get_db(pkg)), cols);
	}
	string_display(_("Name           :"), alpm_pkg_get_name(pkg), cols);
	string_display(_("Version        :"), alpm_pkg_get_version(pkg), cols);
	string_display(_("Description    :"), alpm_pkg_get_desc(pkg), cols);
	string_display(_("Architecture   :"), alpm_pkg_get_arch(pkg), cols);
	string_display(_("URL            :"), alpm_pkg_get_url(pkg), cols);
	list_display(_("Licenses       :"), alpm_pkg_get_licenses(pkg), cols);
	list_display(_("Groups         :"), alpm_pkg_get_groups(pkg), cols);
	deplist_display(_("Provides       :"), alpm_pkg_get_provides(pkg), cols);
	deplist_display(_("Depends On     :"), alpm_pkg_get_depends(pkg), cols);
	optdeplist_display(_("Optional Deps  :"), alpm_pkg_get_optdepends(pkg), cols);
	if(extra || from == ALPM_PKG_FROM_LOCALDB) {
		list_display(_("Required By    :"), requiredby, cols);
	}
	deplist_display(_("Conflicts With :"), alpm_pkg_get_conflicts(pkg), cols);
	deplist_display(_("Replaces       :"), alpm_pkg_get_replaces(pkg), cols);

	size = humanize_size(alpm_pkg_get_size(pkg), 'K', 2, &label);
	if(from == ALPM_PKG_FROM_SYNCDB) {
		printf(_("Download Size  : %6.2f %s\n"), size, label);
	} else if(from == ALPM_PKG_FROM_FILE) {
		printf(_("Compressed Size: %6.2f %s\n"), size, label);
	}

	size = humanize_size(alpm_pkg_get_isize(pkg), 'K', 2, &label);
	printf(_("Installed Size : %6.2f %s\n"), size, label);

	string_display(_("Packager       :"), alpm_pkg_get_packager(pkg), cols);
	string_display(_("Build Date     :"), bdatestr, cols);
	if(from == ALPM_PKG_FROM_LOCALDB) {
		string_display(_("Install Date   :"), idatestr, cols);
		string_display(_("Install Reason :"), reason, cols);
	}
	if(from == ALPM_PKG_FROM_FILE || from == ALPM_PKG_FROM_LOCALDB) {
		string_display(_("Install Script :"),
				alpm_pkg_has_scriptlet(pkg) ?  _("Yes") : _("No"), cols);
	}

	if(from == ALPM_PKG_FROM_SYNCDB && extra) {
		string_display(_("MD5 Sum        :"), alpm_pkg_get_md5sum(pkg), cols);
		string_display(_("SHA256 Sum     :"), alpm_pkg_get_sha256sum(pkg), cols);
		string_display(_("Signatures     :"),
				alpm_pkg_get_base64_sig(pkg) ? _("Yes") : _("None"), cols);
	} else {
		list_display(_("Validated By   :"), validation, cols);
	}

	if(from == ALPM_PKG_FROM_FILE) {
		alpm_siglist_t siglist;
		int err = alpm_pkg_check_pgp_signature(pkg, &siglist);
		if(err && alpm_errno(config->handle) == ALPM_ERR_SIG_MISSING) {
			string_display(_("Signatures     :"), _("None"), cols);
		} else if(err) {
			string_display(_("Signatures     :"),
					alpm_strerror(alpm_errno(config->handle)), cols);
		} else {
			signature_display(_("Signatures     :"), &siglist, cols);
		}
		alpm_siglist_cleanup(&siglist);
	}

	/* Print additional package info if info flag passed more than once */
	if(from == ALPM_PKG_FROM_LOCALDB && extra) {
		dump_pkg_backups(pkg);
	}

	/* final newline to separate packages */
	printf("\n");

	FREELIST(requiredby);
	alpm_list_free(validation);
}

static const char *get_backup_file_status(const char *root,
		const alpm_backup_t *backup)
{
	char path[PATH_MAX];
	const char *ret;

	snprintf(path, PATH_MAX, "%s%s", root, backup->name);

	/* if we find the file, calculate checksums, otherwise it is missing */
	if(access(path, R_OK) == 0) {
		char *md5sum = alpm_compute_md5sum(path);

		if(md5sum == NULL) {
			pm_printf(ALPM_LOG_ERROR,
					_("could not calculate checksums for %s\n"), path);
			return NULL;
		}

		/* if checksums don't match, file has been modified */
		if(strcmp(md5sum, backup->hash) != 0) {
			ret = "MODIFIED";
		} else {
			ret = "UNMODIFIED";
		}
		free(md5sum);
	} else {
		switch(errno) {
			case EACCES:
				ret = "UNREADABLE";
				break;
			case ENOENT:
				ret = "MISSING";
				break;
			default:
				ret = "UNKNOWN";
		}
	}
	return ret;
}

/* Display list of backup files and their modification states
 */
void dump_pkg_backups(alpm_pkg_t *pkg)
{
	alpm_list_t *i;
	const char *root = alpm_option_get_root(config->handle);
	printf(_("Backup Files:\n"));
	if(alpm_pkg_get_backup(pkg)) {
		/* package has backup files, so print them */
		for(i = alpm_pkg_get_backup(pkg); i; i = alpm_list_next(i)) {
			const alpm_backup_t *backup = i->data;
			const char *value;
			if(!backup->hash) {
				continue;
			}
			value = get_backup_file_status(root, backup);
			printf("%s\t%s%s\n", value, root, backup->name);
		}
	} else {
		/* package had no backup files */
		printf(_("(none)\n"));
	}
}

/* List all files contained in a package
 */
void dump_pkg_files(alpm_pkg_t *pkg, int quiet)
{
	const char *pkgname, *root;
	alpm_filelist_t *pkgfiles;
	size_t i;

	pkgname = alpm_pkg_get_name(pkg);
	pkgfiles = alpm_pkg_get_files(pkg);
	root = alpm_option_get_root(config->handle);

	for(i = 0; i < pkgfiles->count; i++) {
		const alpm_file_t *file = pkgfiles->files + i;
		/* Regular: '<pkgname> <root><filepath>\n'
		 * Quiet  : '<root><filepath>\n'
		 */
		if(!quiet) {
			fputs(pkgname, stdout);
			putchar(' ');
		}
		fputs(root, stdout);
		fputs(file->name, stdout);
		putchar('\n');
	}

	fflush(stdout);
}

/* Display the changelog of a package
 */
void dump_pkg_changelog(alpm_pkg_t *pkg)
{
	void *fp = NULL;

	if((fp = alpm_pkg_changelog_open(pkg)) == NULL) {
		pm_printf(ALPM_LOG_ERROR, _("no changelog available for '%s'.\n"),
				alpm_pkg_get_name(pkg));
		return;
	} else {
		/* allocate a buffer to get the changelog back in chunks */
		char buf[CLBUF_SIZE];
		size_t ret = 0;
		while((ret = alpm_pkg_changelog_read(buf, CLBUF_SIZE, pkg, fp))) {
			if(ret < CLBUF_SIZE) {
				/* if we hit the end of the file, we need to add a null terminator */
				*(buf + ret) = '\0';
			}
			fputs(buf, stdout);
		}
		alpm_pkg_changelog_close(pkg, fp);
		putchar('\n');
	}
}

/* vim: set ts=2 sw=2 noet: */
