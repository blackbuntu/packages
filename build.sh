#!/bin/bash

## -------------- ##
## BUILD PACKAGES ##
## -------------- ##

## Create `debs` directory
## -----------------------
mkdir debs

## Define Base Path
## ----------------
basepath=$(pwd)

## Correct user permissions
## ------------------------
find $basepath/. -type f -name "postinst" -exec chmod +x {} \;
find $basepath/. -type f -name "preinst" -exec chmod +x {} \;

## Generate DEB packages
## ---------------------
echo -e "--------------------------------------------------------------"
for basedir in "$basepath"/*;
do
    for deb in "$basedir"/*;
    do
        if test -d "$deb";
        then
            debname="${deb##*/}"
            echo -e "Building $debname DEB package"
            echo -e "--------------------------------------------------------------"
            dpkg-deb --build --root-owner-group $deb
            ar x "$deb.deb"
            unzstd "$basepath/control.tar.zst"
            unzstd "$basepath/data.tar.zst"
            xz --threads=0 --verbose "$basepath/control.tar"
            xz --threads=0 --verbose "$basepath/data.tar"
            rm -f "$deb.deb"
            ar cr "$deb.deb" debian-binary "$basepath/control.tar.xz" "$basepath/data.tar.xz"
            rm -f debian-binary "$basepath/control.tar" "$basepath/control.tar.xz" "$basepath/control.tar.zst" "$basepath/data.tar" "$basepath/data.tar.xz" "$basepath/data.tar.zst"
            mv "$deb.deb" "$basepath/debs"
            echo -e "--------------------------------------------------------------"
        fi
    done
done
