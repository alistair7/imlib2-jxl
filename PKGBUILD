_pkgname=imlib2-jxl
pkgname="${_pkgname}-git"
pkgver=0
pkgrel=1
pkgdesc="JPEG XL loader for imlib2"
arch=(x86_64)
url="https://github.com/alistair7/$_pkgname"
license=(BSD)
depends=(imlib2 libjxl lcms2)
makedepends=(git)
# Conflicts with any non-git version
conflicts=("$_pkgname")
source=("git+https://github.com/alistair7/$_pkgname.git")
md5sums=(SKIP)

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
	install -Dm 644 jxl.so "$pkgdir$(pkg-config imlib2 --variable=libdir)/imlib2/loaders/jxl.so"
	install -Dm644 LICENSE-BSD-ab "$pkgdir/usr/share/licenses/$pkgname/LICENSE-BSD-ab"
	install -Dm644 LICENSE-BSD-dh "$pkgdir/usr/share/licenses/$pkgname/LICENSE-BSD-dh"
}
