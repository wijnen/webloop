# configure.ac - template for the configure script
# Copyright 2005-2023 Bas Wijnen <wijnen@debian.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.71])
AC_INIT([libwebloop],[0.1],[wijnen@debian.org])
AM_INIT_AUTOMAKE

# for substitution in pkgconf files
VERSION=AC_PACKAGE_VERSION

AC_CONFIG_MACRO_DIRS([m4])

AC_PROG_CXX

LT_INIT

AC_CONFIG_FILES(Makefile webloop.pc)

AC_OUTPUT
