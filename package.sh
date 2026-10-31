#!/bin/bash
# ProJ IDE — packages: .deb (Debian/Kali/Ubuntu), .rpm (Fedora), .zip (Windows)
set -e
cd "$(dirname "$0")"
VERSION=1.0.0
mkdir -p dist
rm -rf dist/pkg dist/projide-$VERSION dist/staging
mkdir -p dist/pkg/DEBIAN dist/pkg/usr/bin dist/pkg/usr/share/applications \
         dist/pkg/usr/share/icons/hicolor/scalable/apps \
         dist/staging/usr/bin dist/staging/usr/share/applications \
         dist/staging/usr/share/icons/hicolor/scalable/apps

# icon
cat > dist/proj-ide.svg <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128">
  <rect width="128" height="128" rx="20" fill="#1b1e23"/>
  <rect x="4" y="4" width="120" height="120" rx="17" fill="none" stroke="#2b3038" stroke-width="2"/>
  <text x="64" y="82" font-family="monospace" font-size="52" font-weight="bold" fill="#6d9ee8" text-anchor="middle">P&gt;</text>
</svg>
SVG
cp dist/proj-ide.svg dist/pkg/usr/share/icons/hicolor/scalable/apps/proj-ide.svg
cp dist/proj-ide.svg dist/staging/usr/share/icons/hicolor/scalable/apps/proj-ide.svg

cat > dist/pkg/usr/share/applications/proj-ide.desktop <<'DESK'
[Desktop Entry]
Type=Application
Name=ProJ IDE
Comment=Компактный IDE: локальная сборка, проекты, живой вывод
Exec=projide-app
Icon=proj-ide
Terminal=false
Categories=Development;IDE;
DESK
cp dist/pkg/usr/share/applications/proj-ide.desktop dist/staging/usr/share/applications/

# ---------- DEB ----------
cp projide projide-app dist/pkg/usr/bin/
cat > dist/pkg/DEBIAN/control <<CTRL
Package: projide
Version: $VERSION
Section: devel
Priority: optional
Architecture: amd64
Depends: libwebkit2gtk-4.1-0, libgtk-3-0
Recommends: build-essential, python3
Maintainer: dj <dj@localhost>
Description: ProJ IDE — compact IDE with a local build engine
 Minimal IDE: projects, Monaco-based editor served by an embedded C++
 HTTP engine, local g++/gcc/nasm/python builds with live program I/O,
 trusted compiler auto-install. Ships both a native GTK/WebKit window
 (projide-app) and a server mode that opens the system browser (projide).
CTRL
dpkg-deb --build --root-owner-group dist/pkg "dist/projide_${VERSION}_amd64.deb"

# ---------- RPM ----------
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
cp projide projide-app dist/staging/usr/bin/
cp dist/proj-ide.svg dist/staging/usr/share/icons/hicolor/scalable/apps/proj-ide.svg
tar -C dist/staging -cf ~/rpmbuild/SOURCES/projide-payload.tar .
cat > ~/rpmbuild/SPECS/projide.spec <<SPEC
Name:           projide
Version:        $VERSION
Release:        1%{?dist}
Summary:        ProJ IDE — compact IDE with a local build engine
License:        MIT
BuildArch:      x86_64
Requires:       webkit2gtk4.1, gtk3
Recommends:     gcc-c++, python3

%description
Minimal IDE: Monaco editor served by an embedded C++ HTTP engine, local
g++/gcc/nasm/python builds with live program I/O, trusted compiler
auto-install. Native GTK/WebKit window plus a browser-mode server.

%prep
%build
%install
mkdir -p %{buildroot}/usr/bin %{buildroot}/usr/share/applications %{buildroot}/usr/share/icons/hicolor/scalable/apps
tar -xf %{_sourcedir}/projide-payload.tar -C %{buildroot}

%files
/usr/bin/projide
/usr/bin/projide-app
/usr/share/applications/proj-ide.desktop
/usr/share/icons/hicolor/scalable/apps/proj-ide.svg
SPEC
rpmbuild -bb --target x86_64 ~/rpmbuild/SPECS/projide.spec >/tmp/rpmbuild.log 2>&1 || { tail -10 /tmp/rpmbuild.log; exit 1; }
cp ~/rpmbuild/RPMS/x86_64/projide-$VERSION-1.*.rpm dist/

# ---------- Windows zip ----------
cd dist && zip -q ../dist/ProjIDE-win64.zip "../ProJ-IDE.exe" 2>/dev/null || zip -q ProjIDE-win64.zip "../ProJ-IDE.exe"; cd ..
mv dist/ProjIDE-win64.zip dist/ProjIDE-win64.zip 2>/dev/null || true
rm -f dist/ProjIDE-win64.zip
cd dist && zip -q ProjIDE-win64.zip ProJ-IDE.exe; cd ..
echo "=== dist/ ==="
ls -la dist/*.deb dist/*.rpm dist/*.zip
