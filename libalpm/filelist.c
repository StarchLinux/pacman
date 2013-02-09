/*
 *  filelist.c
 *
 *  Copyright (c) 2012 Pacman Development Team <pacman-dev@archlinux.org>
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

#include <string.h>

/* libalpm */
#include "filelist.h"

/* Returns the difference of the provided two lists of files.
 * Pre-condition: both lists are sorted!
 * When done, free the list but NOT the contained data.
 */
alpm_list_t *_alpm_filelist_difference(alpm_filelist_t *filesA,
		alpm_filelist_t *filesB)
{
	alpm_list_t *ret = NULL;
	size_t ctrA = 0, ctrB = 0;

	while(ctrA < filesA->count && ctrB < filesB->count) {
		alpm_file_t *fileA = filesA->files + ctrA;
		alpm_file_t *fileB = filesB->files + ctrB;
		const char *strA = fileA->name;
		const char *strB = fileB->name;
		/* skip directories, we don't care about them */
		if(strA[strlen(strA)-1] == '/') {
			ctrA++;
		} else if(strB[strlen(strB)-1] == '/') {
			ctrB++;
		} else {
			int cmp = strcmp(strA, strB);
			if(cmp < 0) {
				/* item only in filesA, qualifies as a difference */
				ret = alpm_list_add(ret, fileA);
				ctrA++;
			} else if(cmp > 0) {
				ctrB++;
			} else {
				ctrA++;
				ctrB++;
			}
		}
	}

	/* ensure we have completely emptied pA */
	while(ctrA < filesA->count) {
		alpm_file_t *fileA = filesA->files + ctrA;
		const char *strA = fileA->name;
		/* skip directories */
		if(strA[strlen(strA)-1] != '/') {
			ret = alpm_list_add(ret, fileA);
		}
		ctrA++;
	}

	return ret;
}

/* Returns the intersection of the provided two lists of files.
 * Pre-condition: both lists are sorted!
 * When done, free the list but NOT the contained data.
 */
alpm_list_t *_alpm_filelist_intersection(alpm_filelist_t *filesA,
		alpm_filelist_t *filesB)
{
	alpm_list_t *ret = NULL;
	size_t ctrA = 0, ctrB = 0;

	while(ctrA < filesA->count && ctrB < filesB->count) {
		alpm_file_t *fileA = filesA->files + ctrA;
		alpm_file_t *fileB = filesB->files + ctrB;
		const char *strA = fileA->name;
		const char *strB = fileB->name;
		/* skip directories, we don't care about them */
		if(strA[strlen(strA)-1] == '/') {
			ctrA++;
		} else if(strB[strlen(strB)-1] == '/') {
			ctrB++;
		} else {
			int cmp = strcmp(strA, strB);
			if(cmp < 0) {
				ctrA++;
			} else if(cmp > 0) {
				ctrB++;
			} else {
				/* item in both, qualifies as an intersect */
				ret = alpm_list_add(ret, fileA);
				ctrA++;
				ctrB++;
			}
		}
	}

	return ret;
}

/* Helper function for comparing files list entries
 */
int _alpm_files_cmp(const void *f1, const void *f2)
{
	const alpm_file_t *file1 = f1;
	const alpm_file_t *file2 = f2;
	return strcmp(file1->name, file2->name);
}


alpm_file_t *alpm_filelist_contains(alpm_filelist_t *filelist,
		const char *path)
{
	alpm_file_t key;

	if(!filelist) {
		return NULL;
	}

	key.name = (char *)path;

	return bsearch(&key, filelist->files, filelist->count,
			sizeof(alpm_file_t), _alpm_files_cmp);
}

/* vim: set ts=2 sw=2 noet: */
