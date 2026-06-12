#!/bin/bash

set -eux

# Simplify the building of releases.
#
# Options:
#
# -a: Build an AppImage (x86_64) on Linux.
# -m: Build both x86_64 and ARM DMG on Mac.
# -n: Notarize the resulting binary (Mac only).
# -w: Build Windows releases. MSVC is used for x86_64 and aarch64
#     (Qt 6), MinGW for i686 and armv7 (Qt 5).

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
# with different file sets, which breaks the universal lipo merge below.
#
# Plugins therefore need an explicit allowlist (the one thing that can't be
# scanned). Frameworks, being linked, don't: otool -L tells us exactly which
# are reachable from the executables and the surviving plugins, and anything
# else (e.g. QtVirtualKeyboard, pulled in only by the virtualkeyboard plugin)
# is dead weight. Pruning both bundles the same way also makes their file sets
# identical, which is what lets the lipo merge work.
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
    done < <(find "${app}/Contents/MacOS" "${app}/Contents/PlugIns" -type f -print0)

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

build_appimage=
build_mac=
build_windows=
notarize=

while getopts "amnw" o
do
    case "${o}" in
        a)
            build_appimage=1
            ;;
        m)
            build_mac=1
            ;;
        n)
            notarize=1
            ;;
        w)
            build_windows=1
            ;;
        *)
            fatal "Usage: $0 [-amnw]"
            ;;
    esac
done

if [[ "${build_appimage}" ]]
then
    ./gargoyle-appimage.sh -c
fi

if [[ "${build_mac}" ]]
then
    if ! security show-keychain-info login.keychain > /dev/null 2>&1
    then
        echo "Keychain is locked; unlock before continuing"
        security unlock-keychain login.keychain
    fi

    rm -rf Gargoyle.app Gargoyle-x86_64.app
    PATH=/usr/local/bin:$PATH ./gargoyle_osx.sh -cnq
    mv Gargoyle.app Gargoyle-x86_64.app
    PATH=/opt/homebrew/bin:$PATH ./gargoyle_osx.sh -cnq

    # Drop unused plugins/frameworks from each bundle. This also makes the two
    # bundles symmetric, which the universal merge below relies on.
    prune_bundle Gargoyle-x86_64.app
    prune_bundle Gargoyle.app

    # The lipo loop below walks only the ARM bundle, so a file present
    # only in the x86_64 bundle would be silently dropped (and its
    # x86_64 dependents broken at runtime on Intel) rather than failing
    # the build. Require the two bundles to have identical file sets.
    diff <(cd Gargoyle.app && find . -type f | sort) \
         <(cd Gargoyle-x86_64.app && find . -type f | sort)

    # The set of Mach-O files (and their locations) differs between
    # the Cocoa and Qt layouts: Qt has framework directories and
    # plugin subdirectories, and its terps are in MacOS instead of
    # PlugIns. Instead of hardcoding paths, find all Mach-O files in
    # the bundle. This skips non-binary files (fonts, qt.conf,
    # Info.plist, framework symlinks/resources, the nib).
    binaries=()
    while IFS= read -r -d '' candidate
    do
        if file -b "${candidate}" | grep -q '^Mach-O'
        then
            binaries+=("${candidate}")
        fi
    done < <(find Gargoyle.app -type f -print0)

    for binary in "${binaries[@]}"
    do
        src="Gargoyle-x86_64.app/${binary#Gargoyle.app/}"
        lipo -create "${binary}" "${src}" -output "${binary}"
    done

    codesign -f -o runtime --sign "${APPLE_CERT_NAME}" "${binaries[@]}" Gargoyle.app
    ditto -c -k --keepParent Gargoyle.app Gargoyle.zip

    if [[ "${notarize}" ]]
    then
        xcrun notarytool submit Gargoyle.zip --apple-id "${APPLE_ID}" --team-id "${APPLE_TEAM_ID}" --wait
        xcrun stapler staple Gargoyle.app
    fi

    dmg_filename="Gargoyle-$(<VERSION).dmg"
    rm -f "${dmg_filename}"
    hdiutil create -fs "HFS+J" -ov -srcfolder Gargoyle.app/ "${dmg_filename}"
fi

if [[ "${build_windows}" ]]
then
    for arch in i686 x86_64 aarch64 armv7
    do
        case "${arch}" in
            x86_64|aarch64)
                ./msvc.sh -c -a "${arch}"
                ;;
            *)
                ./windows.sh -c -a "${arch}"
                ;;
        esac
        ./package-windows.sh "${arch}"
    done
fi
