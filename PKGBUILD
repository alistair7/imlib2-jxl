# Maintainer: TODO

# Basde on PKGBUILD-git - https://github.com/jamesan/linux-templates/blob/master/PKGBUILD-git
# template for writing a PKGBUILD file with a git VCS source

# Copyright 2014 James An

# THIS PROGRAM is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

_pkgname=imlib2-jxl
_branch=v0.1.x

pkgname="$_pkgname"
pkgver=0.1.1r14.5ed84f3
pkgrel=1
pkgdesc="JPEG XL loader for imlib2"
arch=('x86_64')
url="https://github.com/alistair7/$_pkgname"
license=('BSD')
depends=('imlib2' 'libjxl' 'lcms2')
makedepends=('git')
provides=("$_pkgname")
conflicts=("$_pkgname-git")
options=()
install=
source=("$_pkgname"::"git+https://github.com/alistair7/$_pkgname.git#branch=$_branch")
md5sums=('SKIP')


pkgver() {
    cd "$_pkgname"
    (
        LATEST_TAG="$(git describe --long --tags 2>/dev/null | sed -r 's/^v?([0-9.]+).*$/\1/')"
        [ -z "$LATEST_TAG" ] && LATEST_TAG=0
        set -o pipefail
        printf "%sr%s.%s" "$LATEST_TAG" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
    )
}

build() {
    cd "$_pkgname"
    make
}

package() {
    cd "$_pkgname"
    install -Dm 644 jxl.so $pkgdir`pkg-config imlib2 --variable=libdir`/imlib2/loaders/jxl.so
    install -Dm644 LICENSE-BSD-ab "$pkgdir/usr/share/licenses/$pkgname/LICENSE-BSD-ab"
    install -Dm644 LICENSE-BSD-dh "$pkgdir/usr/share/licenses/$pkgname/LICENSE-BSD-dh"
}
