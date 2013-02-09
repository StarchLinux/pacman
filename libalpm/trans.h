/*
 *  trans.h
 *
 *  Copyright (c) 2006-2012 Pacman Development Team <pacman-dev@archlinux.org>
 *  Copyright (c) 2002-2006 by Judd Vinet <jvinet@zeroflux.org>
 *  Copyright (c) 2005 by Aurelien Foret <orelien@chez.com>
 *  Copyright (c) 2005 by Christian Hamar <krics@linuxforum.hu>
 *  Copyright (c) 2006 by Miklos Vajna <vmiklos@frugalware.org>
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
#ifndef _ALPM_TRANS_H
#define _ALPM_TRANS_H

#include "alpm.h"

typedef enum _alpm_transstate_t {
	STATE_IDLE = 0,
	STATE_INITIALIZED,
	STATE_PREPARED,
	STATE_DOWNLOADING,
	STATE_COMMITING,
	STATE_COMMITED,
	STATE_INTERRUPTED
} alpm_transstate_t;

/* Transaction */
struct __alpm_trans_t {
	alpm_transflag_t flags;
	alpm_transstate_t state;
	alpm_list_t *unresolvable;  /* list of (alpm_pkg_t *) */
	alpm_list_t *add;           /* list of (alpm_pkg_t *) */
	alpm_list_t *remove;        /* list of (alpm_pkg_t *) */
	alpm_list_t *skip_remove;   /* list of (char *) */
};

void _alpm_trans_free(alpm_trans_t *trans);
int _alpm_trans_init(alpm_trans_t *trans, alpm_transflag_t flags);
int _alpm_runscriptlet(alpm_handle_t *handle, const char *filepath,
		const char *script, const char *ver, const char *oldver, int is_archive);

#endif /* _ALPM_TRANS_H */

/* vim: set ts=2 sw=2 noet: */
