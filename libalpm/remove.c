/*
 *  remove.c
 *
 *  Copyright (c) 2006-2012 Pacman Development Team <pacman-dev@archlinux.org>
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
 *  Copyright (c) 2005 by Aurelien Foret <orelien@chez.com>
 *  Copyright (c) 2005 by Christian Hamar <krics@linuxforum.hu>
 *  Copyright (c) 2006 by David Kimpe <dnaku@frugalware.org>
 *  Copyright (c) 2005, 2006 by Miklos Vajna <vmiklos@frugalware.org>
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
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

/* libalpm */
#include "remove.h"
#include "alpm_list.h"
#include "alpm.h"
#include "trans.h"
#include "util.h"
#include "log.h"
#include "backup.h"
#include "package.h"
#include "db.h"
#include "deps.h"
#include "handle.h"
#include "filelist.h"

/**
 * @brief Add a package removal action to the transaction.
 *
 * @param handle the context handle
 * @param pkg the package to uninstall
 *
 * @return 0 on success, -1 on error
 */
int SYMEXPORT alpm_remove_pkg(alpm_handle_t *handle, alpm_pkg_t *pkg)
{
	const char *pkgname;
	alpm_trans_t *trans;
	alpm_pkg_t *copy;

	/* Sanity checks */
	CHECK_HANDLE(handle, return -1);
	ASSERT(pkg != NULL, RET_ERR(handle, ALPM_ERR_WRONG_ARGS, -1));
	ASSERT(handle == pkg->handle, RET_ERR(handle, ALPM_ERR_WRONG_ARGS, -1));
	trans = handle->trans;
	ASSERT(trans != NULL, RET_ERR(handle, ALPM_ERR_TRANS_NULL, -1));
	ASSERT(trans->state == STATE_INITIALIZED,
			RET_ERR(handle, ALPM_ERR_TRANS_NOT_INITIALIZED, -1));

	pkgname = pkg->name;

	if(_alpm_pkg_find(trans->remove, pkgname)) {
		RET_ERR(handle, ALPM_ERR_TRANS_DUP_TARGET, -1);
	}

	_alpm_log(handle, ALPM_LOG_DEBUG, "adding package %s to the transaction remove list\n",
			pkgname);
	if(_alpm_pkg_dup(pkg, &copy) == -1) {
		return -1;
	}
	trans->remove = alpm_list_add(trans->remove, copy);
	return 0;
}

/**
 * @brief Add dependencies to the removal transaction for cascading.
 *
 * @param handle the context handle
 * @param lp list of missing dependencies caused by the removal transaction
 *
 * @return 0 on success, -1 on error
 */
static int remove_prepare_cascade(alpm_handle_t *handle, alpm_list_t *lp)
{
	alpm_trans_t *trans = handle->trans;

	while(lp) {
		alpm_list_t *i;
		for(i = lp; i; i = i->next) {
			alpm_depmissing_t *miss = i->data;
			alpm_pkg_t *info = _alpm_db_get_pkgfromcache(handle->db_local, miss->target);
			if(info) {
				alpm_pkg_t *copy;
				if(!_alpm_pkg_find(trans->remove, info->name)) {
					_alpm_log(handle, ALPM_LOG_DEBUG, "pulling %s in target list\n",
							info->name);
					if(_alpm_pkg_dup(info, &copy) == -1) {
						return -1;
					}
					trans->remove = alpm_list_add(trans->remove, copy);
				}
			} else {
				_alpm_log(handle, ALPM_LOG_ERROR,
						_("could not find %s in database -- skipping\n"), miss->target);
			}
		}
		alpm_list_free_inner(lp, (alpm_list_fn_free)_alpm_depmiss_free);
		alpm_list_free(lp);
		lp = alpm_checkdeps(handle, _alpm_db_get_pkgcache(handle->db_local),
				trans->remove, NULL, 1);
	}
	return 0;
}

/**
 * @brief Remove needed packages from the removal transaction.
 *
 * @param handle the context handle
 * @param lp list of missing dependencies caused by the removal transaction
 */
static void remove_prepare_keep_needed(alpm_handle_t *handle, alpm_list_t *lp)
{
	alpm_trans_t *trans = handle->trans;

	/* Remove needed packages (which break dependencies) from target list */
	while(lp != NULL) {
		alpm_list_t *i;
		for(i = lp; i; i = i->next) {
			alpm_depmissing_t *miss = i->data;
			void *vpkg;
			alpm_pkg_t *pkg = _alpm_pkg_find(trans->remove, miss->causingpkg);
			if(pkg == NULL) {
				continue;
			}
			trans->remove = alpm_list_remove(trans->remove, pkg, _alpm_pkg_cmp,
					&vpkg);
			pkg = vpkg;
			if(pkg) {
				_alpm_log(handle, ALPM_LOG_WARNING, _("removing %s from target list\n"),
						pkg->name);
				_alpm_pkg_free(pkg);
			}
		}
		alpm_list_free_inner(lp, (alpm_list_fn_free)_alpm_depmiss_free);
		alpm_list_free(lp);
		lp = alpm_checkdeps(handle, _alpm_db_get_pkgcache(handle->db_local),
				trans->remove, NULL, 1);
	}
}

/**
 * @brief Transaction preparation for remove actions.
 *
 * This functions takes a pointer to a alpm_list_t which will be
 * filled with a list of alpm_depmissing_t* objects representing
 * the packages blocking the transaction.
 *
 * @param handle the context handle
 * @param data a pointer to an alpm_list_t* to fill
 *
 * @return 0 on success, -1 on error
 */
int _alpm_remove_prepare(alpm_handle_t *handle, alpm_list_t **data)
{
	alpm_list_t *lp;
	alpm_trans_t *trans = handle->trans;
	alpm_db_t *db = handle->db_local;

	if((trans->flags & ALPM_TRANS_FLAG_RECURSE)
			&& !(trans->flags & ALPM_TRANS_FLAG_CASCADE)) {
		_alpm_log(handle, ALPM_LOG_DEBUG, "finding removable dependencies\n");
		if(_alpm_recursedeps(db, trans->remove,
				trans->flags & ALPM_TRANS_FLAG_RECURSEALL)) {
			return -1;
		}
	}

	if(!(trans->flags & ALPM_TRANS_FLAG_NODEPS)) {
		EVENT(handle, ALPM_EVENT_CHECKDEPS_START, NULL, NULL);

		_alpm_log(handle, ALPM_LOG_DEBUG, "looking for unsatisfied dependencies\n");
		lp = alpm_checkdeps(handle, _alpm_db_get_pkgcache(db), trans->remove, NULL, 1);
		if(lp != NULL) {

			if(trans->flags & ALPM_TRANS_FLAG_CASCADE) {
				if(remove_prepare_cascade(handle, lp)) {
					return -1;
				}
			} else if(trans->flags & ALPM_TRANS_FLAG_UNNEEDED) {
				/* Remove needed packages (which would break dependencies)
				 * from target list */
				remove_prepare_keep_needed(handle, lp);
			} else {
				if(data) {
					*data = lp;
				} else {
					alpm_list_free_inner(lp, (alpm_list_fn_free)_alpm_depmiss_free);
					alpm_list_free(lp);
				}
				RET_ERR(handle, ALPM_ERR_UNSATISFIED_DEPS, -1);
			}
		}
	}

	/* re-order w.r.t. dependencies */
	_alpm_log(handle, ALPM_LOG_DEBUG, "sorting by dependencies\n");
	lp = _alpm_sortbydeps(handle, trans->remove, 1);
	/* free the old alltargs */
	alpm_list_free(trans->remove);
	trans->remove = lp;

	/* -Rcs == -Rc then -Rs */
	if((trans->flags & ALPM_TRANS_FLAG_CASCADE)
			&& (trans->flags & ALPM_TRANS_FLAG_RECURSE)) {
		_alpm_log(handle, ALPM_LOG_DEBUG, "finding removable dependencies\n");
		if(_alpm_recursedeps(db, trans->remove,
					trans->flags & ALPM_TRANS_FLAG_RECURSEALL)) {
			return -1;
		}
	}

	if(!(trans->flags & ALPM_TRANS_FLAG_NODEPS)) {
		EVENT(handle, ALPM_EVENT_CHECKDEPS_DONE, NULL, NULL);
	}

	return 0;
}

/**
 * @brief Check if alpm can delete a file.
 *
 * @param handle the context handle
 * @param file file to be removed
 * @param skip_remove list of files that will not be removed
 *
 * @return 1 if the file can be deleted, 0 if it cannot be deleted
 */
static int can_remove_file(alpm_handle_t *handle, const alpm_file_t *file,
		alpm_list_t *skip_remove)
{
	char filepath[PATH_MAX];

	if(alpm_list_find(skip_remove, file->name, _alpm_fnmatch)) {
		/* return success because we will never actually remove this file */
		return 1;
	}

	snprintf(filepath, PATH_MAX, "%s%s", handle->root, file->name);
	/* If we fail write permissions due to a read-only filesystem, abort.
	 * Assume all other possible failures are covered somewhere else */
	if(_alpm_access(handle, NULL, filepath, W_OK) == -1) {
		if(errno != EACCES && errno != ETXTBSY && access(filepath, F_OK) == 0) {
			/* only return failure if the file ACTUALLY exists and we can't write to
			 * it - ignore "chmod -w" simple permission failures */
			_alpm_log(handle, ALPM_LOG_ERROR, _("cannot remove file '%s': %s\n"),
					filepath, strerror(errno));
			return 0;
		}
	}

	return 1;
}

/**
 * @brief Unlink a package file, backing it up if necessary.
 *
 * @note Helper function for iterating through a package's file and deleting
 * them.
 * @note Used by _alpm_remove_commit.
 *
 * @param handle the context handle
 * @param oldpkg the package being removed
 * @param newpkg the package replacing \a oldpkg
 * @param fileobj file to remove
 * @param skip_remove list of files that shouldn't be removed
 * @param nosave whether files should be backed up
 *
 * @return 0 on success, -1 if there was an error unlinking the file, 1 if the
 * file was skipped or did not exist
 */
static int unlink_file(alpm_handle_t *handle, alpm_pkg_t *oldpkg,
		alpm_pkg_t *newpkg, const alpm_file_t *fileobj, alpm_list_t *skip_remove,
		int nosave)
{
	struct stat buf;
	char file[PATH_MAX];

	snprintf(file, PATH_MAX, "%s%s", handle->root, fileobj->name);

	/* check the remove skip list before removing the file.
	 * see the big comment block in db_find_fileconflicts() for an
	 * explanation. */
	if(alpm_list_find(skip_remove, fileobj->name, _alpm_fnmatch)) {
		_alpm_log(handle, ALPM_LOG_DEBUG,
				"%s is in skip_remove, skipping removal\n", file);
		return 1;
	}

	/* we want to do a lstat here, and not a _alpm_lstat.
	 * if a directory in the package is actually a directory symlink on the
	 * filesystem, we want to work with the linked directory instead of the
	 * actual symlink */
	if(lstat(file, &buf)) {
		_alpm_log(handle, ALPM_LOG_DEBUG, "file %s does not exist\n", file);
		return 1;
	}

	if(S_ISDIR(buf.st_mode)) {
		ssize_t files = _alpm_files_in_directory(handle, file, 0);
		/* if we have files, no need to remove the directory */
		if(files > 0) {
			_alpm_log(handle, ALPM_LOG_DEBUG, "keeping directory %s (contains files)\n",
					file);
		} else if(files < 0) {
			_alpm_log(handle, ALPM_LOG_DEBUG,
					"keeping directory %s (could not count files)\n", file);
		} else if(newpkg && alpm_filelist_contains(alpm_pkg_get_files(newpkg),
					fileobj->name)) {
			_alpm_log(handle, ALPM_LOG_DEBUG,
					"keeping directory %s (in new package)\n", file);
		} else {
			/* one last check- does any other package own this file? */
			alpm_list_t *local, *local_pkgs;
			int found = 0;
			local_pkgs = _alpm_db_get_pkgcache(handle->db_local);
			for(local = local_pkgs; local && !found; local = local->next) {
				alpm_pkg_t *local_pkg = local->data;
				alpm_filelist_t *filelist;

				/* we duplicated the package when we put it in the removal list, so we
				 * so we can't use direct pointer comparison here. */
				if(oldpkg->name_hash == local_pkg->name_hash
						&& strcmp(oldpkg->name, local_pkg->name) == 0) {
					continue;
				}
				filelist = alpm_pkg_get_files(local_pkg);
				if(alpm_filelist_contains(filelist, fileobj->name)) {
					_alpm_log(handle, ALPM_LOG_DEBUG,
							"keeping directory %s (owned by %s)\n", file, local_pkg->name);
					found = 1;
				}
			}
			if(!found) {
				if(rmdir(file)) {
					_alpm_log(handle, ALPM_LOG_DEBUG,
							"directory removal of %s failed: %s\n", file, strerror(errno));
					return -1;
				} else {
					_alpm_log(handle, ALPM_LOG_DEBUG,
							"removed directory %s (no remaining owners)\n", file);
				}
			}
		}
	} else {
		/* if the file needs backup and has been modified, back it up to .pacsave */
		alpm_backup_t *backup = _alpm_needbackup(fileobj->name, oldpkg);
		if(backup) {
			if(nosave) {
				_alpm_log(handle, ALPM_LOG_DEBUG, "transaction is set to NOSAVE, not backing up '%s'\n", file);
			} else {
				char *filehash = alpm_compute_md5sum(file);
				int cmp = filehash ? strcmp(filehash, backup->hash) : 0;
				FREE(filehash);
				if(cmp != 0) {
					char *newpath;
					size_t len = strlen(file) + 8 + 1;
					MALLOC(newpath, len, RET_ERR(handle, ALPM_ERR_MEMORY, -1));
					snprintf(newpath, len, "%s.pacsave", file);
					if(rename(file, newpath)) {
						_alpm_log(handle, ALPM_LOG_ERROR, _("could not rename %s to %s (%s)\n"),
								file, newpath, strerror(errno));
						alpm_logaction(handle, "error: could not rename %s to %s (%s)\n",
								file, newpath, strerror(errno));
						free(newpath);
						return -1;
					}
					_alpm_log(handle, ALPM_LOG_WARNING, _("%s saved as %s\n"), file, newpath);
					alpm_logaction(handle, "warning: %s saved as %s\n", file, newpath);
					free(newpath);
					return 0;
				}
			}
		}

		_alpm_log(handle, ALPM_LOG_DEBUG, "unlinking %s\n", file);

		if(unlink(file) == -1) {
			_alpm_log(handle, ALPM_LOG_ERROR, _("cannot remove %s (%s)\n"),
					file, strerror(errno));
			alpm_logaction(handle, "error: cannot remove %s (%s)\n",
					file, strerror(errno));
			return -1;
		}
	}
	return 0;
}

/**
 * @brief Remove a package's files, optionally skipping its replacement's
 * files.
 *
 * @param handle the context handle
 * @param oldpkg package to remove
 * @param newpkg package to replace \a oldpkg (optional)
 * @param targ_count current index within the transaction (1-based)
 * @param pkg_count the number of packages affected by the transaction
 *
 * @return 0 on success, -1 if alpm lacks permission to delete some of the
 * files, >0 the number of files alpm was unable to delete
 */
static int remove_package_files(alpm_handle_t *handle,
		alpm_pkg_t *oldpkg, alpm_pkg_t *newpkg,
		size_t targ_count, size_t pkg_count)
{
	alpm_list_t *skip_remove;
	alpm_filelist_t *filelist;
	size_t i;
	int err = 0;
	int nosave = handle->trans->flags & ALPM_TRANS_FLAG_NOSAVE;

	if(newpkg) {
		alpm_filelist_t *newfiles;
		alpm_list_t *b;
		skip_remove = alpm_list_join(
				alpm_list_strdup(handle->trans->skip_remove),
				alpm_list_strdup(handle->noupgrade));
		/* Add files in the NEW backup array to the skip_remove array
		 * so this removal operation doesn't kill them */
		/* old package backup list */
		newfiles = alpm_pkg_get_files(newpkg);
		for(b = alpm_pkg_get_backup(newpkg); b; b = b->next) {
			const alpm_backup_t *backup = b->data;
			/* safety check (fix the upgrade026 pactest) */
			if(!alpm_filelist_contains(newfiles, backup->name)) {
				continue;
			}
			_alpm_log(handle, ALPM_LOG_DEBUG, "adding %s to the skip_remove array\n",
					backup->name);
			skip_remove = alpm_list_add(skip_remove, strdup(backup->name));
		}
	} else {
		skip_remove = alpm_list_strdup(handle->trans->skip_remove);
	}

	filelist = alpm_pkg_get_files(oldpkg);
	for(i = 0; i < filelist->count; i++) {
		alpm_file_t *file = filelist->files + i;
		if(!can_remove_file(handle, file, skip_remove)) {
			_alpm_log(handle, ALPM_LOG_DEBUG,
					"not removing package '%s', can't remove all files\n",
					oldpkg->name);
			FREELIST(skip_remove);
			RET_ERR(handle, ALPM_ERR_PKG_CANT_REMOVE, -1);
		}
	}

	_alpm_log(handle, ALPM_LOG_DEBUG, "removing %zd files\n", filelist->count);

	if(!newpkg) {
		/* init progress bar, but only on true remove transactions */
		PROGRESS(handle, ALPM_PROGRESS_REMOVE_START, oldpkg->name, 0,
				pkg_count, targ_count);
	}

	/* iterate through the list backwards, unlinking files */
	for(i = filelist->count; i > 0; i--) {
		alpm_file_t *file = filelist->files + i - 1;
		if(unlink_file(handle, oldpkg, newpkg, file, skip_remove, nosave) < 0) {
			err++;
		}

		if(!newpkg) {
			/* update progress bar after each file */
			int percent = ((filelist->count - i) * 100) / filelist->count;
			PROGRESS(handle, ALPM_PROGRESS_REMOVE_START, oldpkg->name,
					percent, pkg_count, targ_count);
		}
	}
	FREELIST(skip_remove);

	if(!newpkg) {
		/* set progress to 100% after we finish unlinking files */
		PROGRESS(handle, ALPM_PROGRESS_REMOVE_START, oldpkg->name, 100,
				pkg_count, targ_count);
	}

	return err;
}

/**
 * @brief Remove a package from the filesystem.
 *
 * @param handle the context handle
 * @param oldpkg package to remove
 * @param newpkg package to replace \a oldpkg (optional)
 * @param targ_count current index within the transaction (1-based)
 * @param pkg_count the number of packages affected by the transaction
 *
 * @return 0
 */
int _alpm_remove_single_package(alpm_handle_t *handle,
		alpm_pkg_t *oldpkg, alpm_pkg_t *newpkg,
		size_t targ_count, size_t pkg_count)
{
	const char *pkgname = oldpkg->name;
	const char *pkgver = oldpkg->version;

	if(newpkg) {
		_alpm_log(handle, ALPM_LOG_DEBUG, "removing old package first (%s-%s)\n",
				pkgname, pkgver);
	} else {
		EVENT(handle, ALPM_EVENT_REMOVE_START, oldpkg, NULL);
		_alpm_log(handle, ALPM_LOG_DEBUG, "removing package %s-%s\n",
				pkgname, pkgver);

		/* run the pre-remove scriptlet if it exists  */
		if(alpm_pkg_has_scriptlet(oldpkg) &&
				!(handle->trans->flags & ALPM_TRANS_FLAG_NOSCRIPTLET)) {
			char *scriptlet = _alpm_local_db_pkgpath(handle->db_local,
					oldpkg, "install");
			_alpm_runscriptlet(handle, scriptlet, "pre_remove", pkgver, NULL, 0);
			free(scriptlet);
		}
	}

	if(!(handle->trans->flags & ALPM_TRANS_FLAG_DBONLY)) {
		/* TODO check returned errors if any */
		remove_package_files(handle, oldpkg, newpkg, targ_count, pkg_count);
	}

	/* run the post-remove script if it exists  */
	if(!newpkg && alpm_pkg_has_scriptlet(oldpkg) &&
			!(handle->trans->flags & ALPM_TRANS_FLAG_NOSCRIPTLET)) {
		char *scriptlet = _alpm_local_db_pkgpath(handle->db_local,
				oldpkg, "install");
		_alpm_runscriptlet(handle, scriptlet, "post_remove", pkgver, NULL, 0);
		free(scriptlet);
	}

	if(!newpkg) {
		EVENT(handle, ALPM_EVENT_REMOVE_DONE, oldpkg, NULL);
	}

	/* remove the package from the database */
	_alpm_log(handle, ALPM_LOG_DEBUG, "removing database entry '%s'\n", pkgname);
	if(_alpm_local_db_remove(handle->db_local, oldpkg) == -1) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not remove database entry %s-%s\n"),
				pkgname, pkgver);
	}
	/* remove the package from the cache */
	if(_alpm_db_remove_pkgfromcache(handle->db_local, oldpkg) == -1) {
		_alpm_log(handle, ALPM_LOG_ERROR, _("could not remove entry '%s' from cache\n"),
				pkgname);
	}

	/* TODO: useful return values */
	return 0;
}

/**
 * @brief Remove packages in the current transaction.
 *
 * @param handle the context handle
 * @param run_ldconfig whether to run ld_config after removing the packages
 *
 * @return 0 on success, -1 if errors occurred while removing files
 */
int _alpm_remove_packages(alpm_handle_t *handle, int run_ldconfig)
{
	alpm_list_t *targ;
	size_t pkg_count, targ_count;
	alpm_trans_t *trans = handle->trans;
	int ret = 0;

	pkg_count = alpm_list_count(trans->remove);
	targ_count = 1;

	for(targ = trans->remove; targ; targ = targ->next) {
		alpm_pkg_t *pkg = targ->data;

		if(trans->state == STATE_INTERRUPTED) {
			return ret;
		}

		if(_alpm_remove_single_package(handle, pkg, NULL,
					targ_count, pkg_count) == -1) {
			handle->pm_errno = ALPM_ERR_TRANS_ABORT;
			/* running ldconfig at this point could possibly screw system */
			run_ldconfig = 0;
			ret = -1;
		}

		targ_count++;
	}

	if(run_ldconfig) {
		/* run ldconfig if it exists */
		_alpm_ldconfig(handle);
	}

	return ret;
}

/* vim: set ts=2 sw=2 noet: */
