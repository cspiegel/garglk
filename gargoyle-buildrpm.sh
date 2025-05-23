#!/bin/bash
# This scripts creates and builds a simple Gargoyle RPM package
#
# Prerequisites:
#  - rpm-build, make and gcc (as it's a c file) packages must be installed
#

set -e

frankendrift="OFF"

while getopts "f" o
do
    case "${o}" in
        f)
            frankendrift_spec="%{_libexecdir}/gargoyle/FrankenDrift.GlkRunner.Gargoyle"
            frankendrift="ON"
            ;;
        *)
            fatal "Usage: $0 [-f]"
            ;;
    esac
done

VERSION=$(<VERSION)

# Holds the name of the root directory containing the necessary structure to
# build RPM packages.
RPM_ROOT_DIR=~/rpm_factory
PKG_NAME=gargoyle-free
PKG_TAR=/tmp/${PKG_NAME}.tar.gz
PKG_DIR=/tmp/${PKG_NAME}

#Build Gargoyle
mkdir build-rpm
pushd build-rpm
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DWITH_FRANKENDRIFT="${frankendrift}"
make -j$(nproc)
make install DESTDIR=${PKG_DIR}
popd

cp -v ./garglk/garglk.ini ${PKG_DIR}

#Compress the package directory with all distribution elements in it as an input to RPM build process.
pushd ${PKG_DIR}/..
tar czvf ${PKG_NAME}.tar.gz ${PKG_NAME}
popd

# Recreate the root directory and its structure if necessary.  Note: You can find the final RPM in ~/rpm_factory/RPMS.
mkdir -p ${RPM_ROOT_DIR}/{SOURCES,BUILD,RPMS,SPECS,SRPMS,tmp}
pushd  $RPM_ROOT_DIR
cp ${PKG_TAR} ${RPM_ROOT_DIR}/SOURCES/

# Creating a basic spec file for Gargoyle
cat << __EOF__ > ${RPM_ROOT_DIR}/SPECS/${PKG_NAME}.spec
Summary: This package contains a build of the Gargoyle interactive fiction interpreter. This build is based on sources available at https://github.com/garglk/garglk.
Name: $PKG_NAME
Version: $VERSION
Release: 0
License: GPL
Packager: Chris Spiegel <cspiegel@gmail.com>
Source: %{name}.tar.gz
BuildRequires: coreutils
BuildRoot: ${RPM_ROOT_DIR}/tmp/%{name}-%{version}
Provides: libgarglk.so()(64bit)

%global debug_package %{nil}

%description
%{summary}

%prep
%setup -n ${PKG_NAME}

%build

%install
cp -av usr "%{buildroot}"
mkdir -p "%{buildroot}%{_sysconfdir}/"
cp -v garglk.ini "%{buildroot}%{_sysconfdir}/"

%files
%{_sysconfdir}/garglk.ini
%{_libdir}/libgarglk.so
%{_libdir}/libgarglk-gpl2.so
${frankendrift_spec}
%{_libexecdir}/gargoyle/advsys
%{_libexecdir}/gargoyle/magnetic
%{_libexecdir}/gargoyle/agility
%{_libexecdir}/gargoyle/git
%{_libexecdir}/gargoyle/alan2
%{_libexecdir}/gargoyle/glulxe
%{_libexecdir}/gargoyle/scare
%{_libexecdir}/gargoyle/alan3
%{_libexecdir}/gargoyle/hugo
%{_libexecdir}/gargoyle/plus
%{_libexecdir}/gargoyle/scott
%{_libexecdir}/gargoyle/taylor
%{_libexecdir}/gargoyle/bocfel
%{_libexecdir}/gargoyle/jacl
%{_libexecdir}/gargoyle/tadsr
%{_libexecdir}/gargoyle/level9
%{_bindir}/gargoyle
%{_datarootdir}/applications/io.github.garglk.Gargoyle.desktop
%{_datarootdir}/applications/io.github.garglk.GargoyleEditConfig.desktop
%{_datarootdir}/gargoyle/themes/Blue.json
"%{_datarootdir}/gargoyle/themes/Breeze Darker.json"
"%{_datarootdir}/gargoyle/themes/Lectrote Dark.json"
"%{_datarootdir}/gargoyle/themes/Lectrote Sepia.json"
"%{_datarootdir}/gargoyle/themes/Lectrote Slate.json"
%{_datarootdir}/gargoyle/Gargoyle-Mono-Bold-Italic.ttf
%{_datarootdir}/gargoyle/Gargoyle-Mono-Bold.ttf
%{_datarootdir}/gargoyle/Gargoyle-Mono-Italic.ttf
%{_datarootdir}/gargoyle/Gargoyle-Mono.ttf
%{_datarootdir}/gargoyle/Gargoyle-Serif-Bold-Italic.ttf
%{_datarootdir}/gargoyle/Gargoyle-Serif-Bold.ttf
%{_datarootdir}/gargoyle/Gargoyle-Serif-Italic.ttf
%{_datarootdir}/gargoyle/Gargoyle-Serif.ttf
%{_datarootdir}/gargoyle/themes/Pencil.json
%{_datarootdir}/gargoyle/themes/Zoom.json
%{_datarootdir}/gargoyle/themes/dark.json
%{_datarootdir}/gargoyle/themes/light.json
%{_datarootdir}/gargoyle/unifont.otf
%{_datarootdir}/gargoyle/unifont_upper.otf
%{_datarootdir}/icons/io.github.garglk.Gargoyle.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-adrift.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-advsys.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-agt.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-alan-adventure-game.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-alan3-adventure-game.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-blorb.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-glulx.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-hugo-image.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-level9.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-magscroll.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-tads.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-t3vm-image.png
%{_datarootdir}/icons/hicolor/32x32/mimetypes/application-x-zmachine.png
%{_datarootdir}/man/man6/gargoyle.6.gz
%{_datarootdir}/metainfo/io.github.garglk.Gargoyle.appdata.xml
%{_datarootdir}/mime/packages/gargoyle.xml

%clean
%if "%{clean}" != ""
  rm -rfv %{_topdir}/BUILD/%{name}
%endif

%post
__EOF__

rpmbuild -v -bb --define "_topdir ${RPM_ROOT_DIR}" SPECS/${PKG_NAME}.spec
popd
