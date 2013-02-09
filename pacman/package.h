/*
 *  package.h
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
#ifndef _PM_PACKAGE_H
#define _PM_PACKAGE_H

#include <alpm.h>

void dump_pkg_full(alpm_pkg_t *pkg, int extra);

void dump_pkg_backups(alpm_pkg_t *pkg);
void dump_pkg_files(alpm_pkg_t *pkg, int quiet);
void dump_pkg_changelog(alpm_pkg_t *pkg);

#endif /* _PM_PACKAGE_H */

/* vim: set ts=2 sw=2 noet: */
