#!/bin/mksh

. /etc/makepkg.conf

function msg {
	echo "$@" >&2
}

function fetchsrc {
	unset failed
	for x in "${source[@]}"; do
		test -f "$x" || test -f "$(basename "$x")" && continue
		echo "$x" | sed -En 's/^[^:]+:/&/p' | grep -E "$(curl -V | sed -n 's/^Protocols: //p' | sed 's/ /|/g')" >/dev/null && {
			curl -LOC - "$x" || failed=yes
			continue
		}
		msg "Unknown URL type:" $(grep -E '^[^:]+' <<<"$s")
		failed=yes
	done
	if set | grep '^failed=' >/dev/null; then return 1; else return 0; fi
}

function chksrc {
	test "${#sources[@]}" -eq 0 && return 0
	sumses=($(set | sed -n 's/^[^=]+sums=/&/p' | tr -d '='))
	test "${#sumses[@]}" -eq 0 && {
		msg "No source checksums"
		exit 1
	}
	test "${#sumses[@]}" -gt 1 && {
		msg "Too many source checksum lists"
		exit 1
	}
	sumtype=${sumses[1]}
	sums=("${$sumtype[@]}")
	test "${#source[@]}" -eq "${#sums[@]}" || {
		msg "Length mismatch of source and checksum arrays"
		exit 1
	}
	for n in $(seq 1 ${#source[@]}); do
		diff $(hash.$sumtype <"${source[$n]}") "${sums[$n]}" >/dev/null || {
			msg "File '${source[$n]}' invalid or corrupted"
			return 1
		}
	done
	return 0
}

function unpksrc1 {
	unset failed
	y="$(basename "$1")"
	case "$y" in
		(*.gz)	gzip  -df "$y" || failed=yes;|
		(*.bz2)	bzip2 -df "$y" || failed=yes;|
		(*.xz)	xz    -df "$y" || failed=yes;|
		(*.lzo)	lzop  -df "$y" || failed=yes;|
		(*.@(gz|bz2|xz|lzo))
			unpksrc1 "$(sed -E 's/\.[^.]+$//' <<<"$y")";;
		(*.tar)
			tar -xC src <"$y" || failed=yes;;
		(*.cpio)
			(chdir src; cpio -di <"$y") || failed=yes;;
		(*)
			cp -R "$y" "$srcdir/" || failed=yes;;
	esac
	if set | grep '^failed=' >/dev/null; then return 1; else return 0; fi
}

function unpksrc {
	unset failed
	mkdir -p "$srcdir" || exit $?
	for x in "${source[@]}"; do unpksrc1 "$x" || failed=yes; done
	if set | grep '^failed=' >/dev/null; then return 1; else return 0; fi
}

function writePkginfo {
	set | grep '^PACKAGER=' >/dev/null || PACKAGER=Unknown
	echo "pkgname = $pkgname"
	echo "pkgver = $pkgver"
	echo "pkgdesc = $pkgdesc"
	echo "url = $url"
	echo "builddate = $(date -u "+%s")"
	echo "packager = $PACKAGER"
	echo "size = $(dc -e "$(du -sk | cut -f 1) 1024 * p")"
	echo "arch = $pkgarch"
	
	for x in "${groups[@]}";     do echo "group = $x"; done
	for x in "${backup[@]}";     do echo "backup = $x"; done
	for x in "${replaces[@]}";   do echo "replaces = $x"; done
	for x in "${conflicts[@]}";  do echo "conflict = $x"; done
	for x in "${provides[@]}";   do echo "provides = $x"; done
	for x in "${depends[@]}";    do echo "depend = $x"; done
	for x in "${optdepends[@]}"; do echo "optdepend = $x"; done
}

function createPackage {
	test -d "$pkgdir" || {
		msg "Package directory not found: $pkgdir"
		exit 1
	}
	
	msg "Creating package..."

	set | grep '^install=' >/dev/null && cp "$install" "$pkgdir/.INSTALL"

	chdir "$pkgdir"
	
	writePkginfo >.PKGINFO
	comp_files=('.PKGINFO')
	
	test -f .INSTALL && comp_files+=('.INSTALL')
	
	test -z "$PKGDEST" && PKGDEST="$startdir"
	pkg_file="$PKGDEST/${pkgname}-${pkgver}-${pkgrel}-${pkgarch}$PKGEXT"
	case "$PKGEXT" {
		(*.tar*)	tar -c "${comp_files[@]}" *;;
		(*)		msg "Invalid package extension: $PKGEXT"; exit 1;;
	} |
	case "$PKGEXT" {
		(*.gz)		gzip -n;;
		(*.bz2)		bzip2;;
		(*.xz)		xz;;
	} >"$pkg_file" || {
		msg "Failed to create package file"
		exit 1
	}
}

unset AFlag RFlag dFlag eFlag oFlag
while getopts 'ARdeop:' o; do
	case $o in
		(p)	BUILDSCRIPT="$OPTARG";;
		(*)	${o}Flag=yes;;
	esac
done

set | grep '^BUILDSCRIPT=' >/dev/null || BUILDSCRIPT=./PKGBUILD

test -f "$BUILDSCRIPT" || {
	msg "File not found: $BUILDSCRIPT"
	exit 1
}

. "$BUILDSCRIPT"

startdir="$(pwd)"
srcdir="$startdir/src"
pkgdir="$startdir/pkg"

unset pkgarch
for a in "${arch[@]}"; do
	case "$a" in
		(@(any|$CARCH))	pkgarch="$a";;
	esac
done
set | grep -E '^(pkgarch|AFlag)=' >/dev/null || {
	msg "Package $pkgname unavailable for $CARCH architecture"
	exit 1
}

set | grep '^RFlag=' >/dev/null || {
	set | grep '^dFlag=' >/dev/null || ! set | grep '^makedepends=' || test "${#makedepends[@]}" -eq 0 || pacman -Q "${makedepends[@]}" || {
		msg "Not all packages needed to make $pkgname were found."
		exit 1
	} && {
		set | grep '^eFlag=' >/dev/null || {
			msg "Fetching sources..."
			fetchsrc || exit 1
			msg "Checking source integrity..."
			chksrc   || exit 1
			msg "Unpacking sources..."
			unpksrc  || exit 1
		}
	} && if set | grep '^oFlag='; then exit 0; fi && chdir src && {
		msg "Building..."
		set -e
		build
		if typeset +f | grep '^check$';   then check;   fi
		rm -rf "$pkgdir"
		if typeset +f | grep '^package$'; then package; fi
		set +e
	} && chdir "$startdir"
} && createPackage
