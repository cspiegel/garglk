#!/bin/bash

set -eu

fatal() {
    echo "${@}" >&2
    exit 1
}

# Slim a deployed macOS bundle down to the Qt plugins and frameworks it
# actually uses. macdeployqt copies whole plugin categories (virtualkeyboard,
# tls, networkinformation, every image format, ...) regardless of what the app
# links, because plugins are dlopen'd, not linked, so there's no dependency
# edge to follow. That bloats the bundle and, because the two Homebrew prefixes
# don't have identical Qt packages installed, leaves the x86_64 and ARM bundles
# with different file sets, which breaks the universal lipo merge in release.sh.
#
# Plugins therefore need an explicit allowlist (the one thing that can't be
# scanned). Frameworks, being linked, don't: otool -L tells us exactly which
# are reachable from the executables and the surviving plugins, and anything
# else (e.g. QtVirtualKeyboard, pulled in only by the virtualkeyboard plugin)
# is dead weight. Pruning every bundle the same way also makes the per-arch file
# sets identical, which is what lets release.sh's lipo merge work.
prune_bundle() {
    local app
    app="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
    local fw_dir="${app}/Contents/Frameworks"

    # PNG is built into QtGui, so JPEG is the only image format plugin needed;
    # the network code is purely local, so no tls/networkinformation plugins.
    local keep_plugins="platforms/libqcocoa.dylib styles/libqmacstyle.dylib multimedia/libdarwinmediaplugin.dylib imageformats/libqjpeg.dylib"

    local plugin rel keep k
    if [[ -d "${app}/Contents/PlugIns" ]]
    then
        while IFS= read -r -d '' plugin
        do
            rel="${plugin#"${app}"/Contents/PlugIns/}"
            keep=
            for k in ${keep_plugins}
            do
                if [[ "${rel}" == "${k}" ]]
                then
                    keep=1
                fi
            done
            if [[ -z "${keep}" ]]
            then
                rm -f "${plugin}"
            fi
        done < <(find "${app}/Contents/PlugIns" -type f -name '*.dylib' -print0)
        find "${app}/Contents/PlugIns" -type d -empty -delete
    fi

    # Transitive otool -L closure rooted at the executables and surviving
    # plugins. Use an index into a grow-only queue (rather than slicing) so
    # this stays compatible with the bash 3.2 that ships on macOS.
    local seen=$'\n'
    local queue=() i=0 cur dep resolved root entry s
    while IFS= read -r -d '' root
    do
        if file -b "${root}" | grep -q '^Mach-O'
        then
            queue+=("${root}")
        fi
    done < <(find "${app}/Contents/MacOS" "${app}/Contents/PlugIns" -type f -print0 2>/dev/null)

    while [[ "${i}" -lt "${#queue[@]}" ]]
    do
        cur="${queue[${i}]}"
        i=$((i + 1))
        case "${seen}" in
            *$'\n'"${cur}"$'\n'*) continue ;;
        esac
        seen="${seen}${cur}"$'\n'
        while IFS= read -r dep
        do
            case "${dep}" in
                @rpath/*)           resolved="${fw_dir}/${dep#@rpath/}" ;;
                @loader_path/*)     resolved="$(dirname "${cur}")/${dep#@loader_path/}" ;;
                @executable_path/*) resolved="${app}/Contents/MacOS/${dep#@executable_path/}" ;;
                *)                  continue ;;
            esac
            if [[ -e "${resolved}" ]]
            then
                queue+=("$(cd "$(dirname "${resolved}")" && pwd)/$(basename "${resolved}")")
            fi
        done < <(otool -L "${cur}" | tail -n +2 | awk '{print $1}')
    done

    if [[ -d "${fw_dir}" ]]
    then
        for entry in "${fw_dir}"/*
        do
            if [[ ! -e "${entry}" ]]
            then
                continue
            fi
            keep=
            while IFS= read -r s
            do
                if [[ -n "${s}" && ( "${s}" == "${entry}" || "${s}" == "${entry}"/* ) ]]
                then
                    keep=1
                    break
                fi
            done <<< "${seen}"
            if [[ -z "${keep}" ]]
            then
                echo "pruning unused framework: ${entry#"${app}"/}" >&2
                rm -rf "${entry}"
            fi
        done
    fi
}

GARGOYLE_CLEAN=
GARGOYLE_FRANKENDRIFT="OFF"
GARGOYLE_INTERFACE="COCOA"
GARGOYLE_NO_DMG=
GARGOYLE_SOUND="SDL3"
GARGOYLE_SCARE="OFF"
GARGOYLE_CMAKE_EXTRAS=""

while getopts "2cfnqs" o
do
    case "${o}" in
        2)
            GARGOYLE_SOUND="SDL2"
            ;;
        c)
            GARGOYLE_CLEAN=1
            ;;
        f)
            GARGOYLE_FRANKENDRIFT="ON"
            ;;
        n)
            GARGOYLE_NO_DMG=1
            ;;
        q)
            # Build with the Qt interface instead of Cocoa. When
            # switching interfaces, a clean build (-c) is recommended.
            GARGOYLE_INTERFACE="QT"
            ;;
        s)
            GARGOYLE_SCARE="ON"
            ;;
        *)
            fatal "Usage: $0 [-2cfnqs]"
            ;;
    esac
done

# Use Homebrew if available. Alternately, you could just set the variable to
# either yes or no.
MAC_USEHOMEBREW=${MAC_USEHOMEBREW:-}
if [ "${MAC_USEHOMEBREW}" == "" ]; then
  MAC_USEHOMEBREW=no
  brew --prefix > /dev/null 2>&1 && MAC_USEHOMEBREW=yes
fi

if [ "${MAC_USEHOMEBREW}" == "yes" ]; then
  command -v brew &> /dev/null || fatal "Homebrew requested but not found"
  HOMEBREW_OR_MACPORTS_LOCATION="$(brew --prefix)"
else
  command -v port &> /dev/null || fatal "Neither Homebrew nor MacPorts is available"
  HOMEBREW_OR_MACPORTS_LOCATION="$(cd "$(dirname "$(which port)")/.." && pwd)"
fi

HOST_ARCH="$(uname -m)"

if [[ "${MAC_USEHOMEBREW}" == "yes" ]]
then
    echo "Probing Homebrew architecture..."
    HOMEBREW_ARCH=$(brew config | grep "^macOS:" | cut -d "-" -f2)
else
    HOMEBREW_ARCH=""
fi

# If the Homebrew architecture in $PATH is not the same as the current
# architecture, assume a cross compile. Cross compiling is currently only
# supported on Homebrew, and only from arm64 to x86_64.
case "${HOMEBREW_ARCH}" in
    # Building for x86_64
    "x86_64")
        case "${HOST_ARCH}" in
            x86_64)
                ;;
            arm64)
                echo "Cross compiling to ${HOMEBREW_ARCH} from ${HOST_ARCH}"
                GARGOYLE_CMAKE_EXTRAS="-DCMAKE_OSX_ARCHITECTURES=${HOMEBREW_ARCH} -DCMAKE_PREFIX_PATH=${HOMEBREW_OR_MACPORTS_LOCATION}"
                ;;
            *)
                fatal "Don't know how to cross compile for ${HOMEBREW_ARCH} from ${HOST_ARCH}"
                ;;
        esac

        TARGET_ARCH="${HOMEBREW_ARCH}"
        ;;

    # Building for arm64
    "arm64")
        [[ "${HOST_ARCH}" != "arm64" ]] && fatal "Don't know how to cross compile to ${HOMEBREW_ARCH} from ${HOST_ARCH}"

        TARGET_ARCH="${HOMEBREW_ARCH}"
        ;;

    # No support for cross compiling on MacPorts at the moment. Assume the
    # current architecture.
    "")
        TARGET_ARCH="${HOST_ARCH}"
        ;;

    *)
        fatal "Unknown target architecture: ${HOMEBREW_ARCH}"
        ;;
esac

# The Qt interface uses Qt for sound as well, so that the whole stack
# (including the plugins deployed by macdeployqt) is Qt. This overrides
# the SDL default (or an explicit -2).
if [[ "${GARGOYLE_INTERFACE}" == "QT" ]]
then
    GARGOYLE_SOUND="QT"
fi

# Ensure a sane environment (mainly be certain GNU programs aren't visible).
export PATH="${HOMEBREW_OR_MACPORTS_LOCATION}/bin:/usr/bin:/bin:/usr/sbin"

MACOS_MIN_VER="10.15"

echo "MACOS_MIN_VER $MACOS_MIN_VER"

# Use as many CPU cores as possible
NUMJOBS=$(sysctl -n hw.ncpu)

GARGDIST=build/dist
BUNDLE=Gargoyle.app/Contents

GARVERSION=$(<VERSION)

rm -rf Gargoyle.app
mkdir -p "$BUNDLE/MacOS"
mkdir -p "$BUNDLE/Frameworks"
mkdir -p "$BUNDLE/Resources/Fonts"
mkdir -p "$BUNDLE/Resources/themes"
mkdir -p "$BUNDLE/PlugIns"

[[ -n "${GARGOYLE_CLEAN}" ]] && rm -rf build-osx build/dist

rm -rf $GARGDIST
mkdir -p build-osx
cd build-osx
cmake .. -DBUILD_SHARED_LIBS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_MIN_VER} -DDIST_INSTALL=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_FIND_FRAMEWORK=LAST -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DSOUND="${GARGOYLE_SOUND}" -DWITH_FRANKENDRIFT="${GARGOYLE_FRANKENDRIFT}" -DWITH_SCARE="${GARGOYLE_SCARE}" -DINTERFACE="${GARGOYLE_INTERFACE}" ${GARGOYLE_CMAKE_EXTRAS}
make "-j${NUMJOBS}"
make install
cd -

# Copy the main executable to the MacOS directory;
cp "$GARGDIST/gargoyle" "$BUNDLE/MacOS/Gargoyle"

# Copy terps: the Cocoa launcher loads them from the bundle's PlugIns
# directory, while the Qt launcher looks in its own directory.
if [[ "${GARGOYLE_INTERFACE}" == "QT" ]]
then
    TERP_DIR="$BUNDLE/MacOS"
else
    TERP_DIR="$BUNDLE/PlugIns"
fi
find "${GARGDIST}" -type f -not -name '*.dylib' -not -name 'gargoyle' -print0 | xargs -0 -J @ cp @ "$TERP_DIR"

# Copy the dylibs built to the Frameworks directory.
find "${GARGDIST}" -type f -name '*.dylib' -exec cp {} "$BUNDLE/Frameworks" \;

echo "Copying all required dylibs..."

# List the Homebrew/MacPorts dylibs a file references. For Qt builds,
# Qt frameworks (and their plugins) are deployed by macdeployqt
# instead, so leave them out here.
homebrew_dylibs() {
  if [[ "${GARGOYLE_INTERFACE}" == "QT" ]]
  then
    otool -L "${1}" | grep -F "${HOMEBREW_OR_MACPORTS_LOCATION}" | grep -v '\.framework/' | sed -E -e 's/^[[:space:]]+(.*)[[:space:]]+\([^)]*\)$/\1/'
  else
    otool -L "${1}" | grep -F "${HOMEBREW_OR_MACPORTS_LOCATION}" | sed -E -e 's/^[[:space:]]+(.*)[[:space:]]+\([^)]*\)$/\1/'
  fi
}

PREVIOUS_UNIQUE_DYLIB_PATHS="$(mktemp -t gargoylebuild)"
copy_new_dylibs() {
  # Get the dylibs needed.
  ALL_DYLIB_PATHS="$(mktemp -t gargoylebuild)"
  find "${BUNDLE}" -type f -print0 | while IFS= read -r -d "" file
  do
    homebrew_dylibs "${file}" >> "${ALL_DYLIB_PATHS}"
  done
  UNIQUE_DYLIB_PATHS="$(mktemp -t gargoylebuild)"
  sort "${ALL_DYLIB_PATHS}" | uniq > "${UNIQUE_DYLIB_PATHS}"
  rm "${ALL_DYLIB_PATHS}"

  # Compare the list to the previous one.
  if diff -q "${PREVIOUS_UNIQUE_DYLIB_PATHS}" "${UNIQUE_DYLIB_PATHS}" > /dev/null ; then
    rm "${PREVIOUS_UNIQUE_DYLIB_PATHS}"
    rm "${UNIQUE_DYLIB_PATHS}"
    return 0
  fi

  cp "${UNIQUE_DYLIB_PATHS}" "${PREVIOUS_UNIQUE_DYLIB_PATHS}"

  # Copy dylibs to the Frameworks directory.
  while IFS= read -r dylib
  do
    cp "${dylib}" "$BUNDLE/Frameworks"
    chmod 644 "$BUNDLE/Frameworks/$(basename "${dylib}")"
  done < "${UNIQUE_DYLIB_PATHS}"
  return 1
}
until copy_new_dylibs ; do true; done

echo "Changing dylib IDs and references..."

# Change the dylib IDs in Frameworks.
find "${BUNDLE}/Frameworks" -type f -exec install_name_tool -id "@executable_path/../Frameworks/$(basename "{}")" {} \;

# Use the dylibs in Frameworks.
find "${BUNDLE}" -type f -print0 | while IFS= read -r -d "" file_path
do
  # Replace dylib paths.
  for original_dylib_path in $(homebrew_dylibs "${file_path}"); do
    install_name_tool -change "${original_dylib_path}" "@executable_path/../Frameworks/$(basename "${original_dylib_path}")" "${file_path}"
  done
done

# Use the built dylibs.
find "${BUNDLE}" -type f -print0 | while IFS= read -r -d "" file_path
do
  find "${GARGDIST}" -type f -name '*.dylib' -exec install_name_tool -change "@executable_path/$(basename "{}")" "@executable_path/../Frameworks/$(basename "{}")" "${file_path}" \;
done

# Ensure interpreters can find libgarglk
find "$TERP_DIR" -type f -not -name 'Gargoyle' -exec install_name_tool -add_rpath '@executable_path/../Frameworks' {} \;
install_name_tool -add_rpath '@executable_path/../Frameworks' "$BUNDLE/MacOS/Gargoyle"

# Written before deploying Qt, since macdeployqt reads it to locate the
# bundle's main executable.
/usr/bin/sed -E -e "s/INSERT_VERSION_HERE/$GARVERSION/" garglk/launcher.plist > $BUNDLE/Info.plist

if [[ "${GARGOYLE_INTERFACE}" == "QT" ]]
then
    echo "Deploying Qt..."
    MACDEPLOYQT="$(command -v macdeployqt || command -v macdeployqt6 || echo "${HOMEBREW_OR_MACPORTS_LOCATION}/libexec/qt6/bin/macdeployqt")"
    [[ -x "${MACDEPLOYQT}" ]] || fatal "macdeployqt not found"

    # All executables (the launcher and the terps) need their Qt
    # references fixed up.
    MACDEPLOYQT_ARGS=()
    while IFS= read -r -d "" file
    do
        MACDEPLOYQT_ARGS+=("-executable=${file}")
    done < <(find "$BUNDLE/MacOS" -type f -print0)

    # macdeployqt only understands absolute, @rpath, and @loader_path
    # references; it logs an "Unexpected prefix" error for each
    # @executable_path reference and skips it. Skipping is correct
    # here: those are the dylibs this script already deployed and
    # rewrote above, needing no further deployment. Filter the noise.
    "${MACDEPLOYQT}" Gargoyle.app "${MACDEPLOYQT_ARGS[@]}" \
        2> >(grep -v 'Unexpected prefix "@executable_path"' >&2)

    # Interpreters are plain executables rather than the bundle's main
    # binary, so they don't see the qt.conf that macdeployqt puts in
    # Resources; give them one next to the binaries so they can find
    # the Qt plugins (e.g. the cocoa platform plugin).
    printf '[Paths]\nPlugins = ../PlugIns\n' > "$BUNDLE/MacOS/qt.conf"

    prune_bundle Gargoyle.app
fi

echo "Copying additional support files..."

if [[ "${GARGOYLE_INTERFACE}" == "COCOA" ]]
then
    cp garglk/launchmac.nib "$BUNDLE/Resources/MainMenu.nib"
fi

cp garglk/garglk.ini "$BUNDLE/Resources"
cp garglk/*.icns "$BUNDLE/Resources"
cp licenses/* "$BUNDLE/Resources"

cp fonts/Gargoyle*.ttf $BUNDLE/Resources/Fonts
cp fonts/unifont*.otf $BUNDLE/Resources
cp themes/*.json $BUNDLE/Resources/themes

# The Qt interface looks for the bundled fonts next to the executable.
if [[ "${GARGOYLE_INTERFACE}" == "QT" ]]
then
    cp fonts/Gargoyle*.ttf fonts/unifont*.otf "$BUNDLE/MacOS"
fi

codesign -s - -f --deep Gargoyle.app

# Register the freshly built bundle with LaunchServices so Finder shows
# its icon right away. Without this, Finder can display a generic
# placeholder for a rebuilt bundle even though its icon is set correctly
# (the Dock, which reads the bundle directly at launch, shows it fine);
# this is just a stale icon cache, made worse by leftover registrations
# of previous builds (including mounted DMGs).
LSREGISTER="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [[ -x "${LSREGISTER}" ]]
then
    touch Gargoyle.app
    "${LSREGISTER}" -f Gargoyle.app
fi

if [[ "${GARGOYLE_INTERFACE}" == "QT" ]]
then
    DMG_NAME="gargoyle-qt-$GARVERSION-$TARGET_ARCH.dmg"
else
    DMG_NAME="gargoyle-$GARVERSION-$TARGET_ARCH.dmg"
fi

if [[ -z "${GARGOYLE_NO_DMG}" ]]
then
    echo "Creating DMG..."
    hdiutil create -fs "HFS+J" -ov -srcfolder Gargoyle.app/ "${DMG_NAME}"
fi

echo "Done."
