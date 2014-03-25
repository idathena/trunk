#!/bin/sh
#source var/function
. ./function.sh

#read -p "WARNING: This target is experimental. Press Ctrl+C to cancel or Enter to continue." readEnterKey

# NOTE: This requires GNU getopt.  On Mac OS X and FreeBSD, you have to install this
# separately; see below.
TEMP=`getopt -o d: -l destdir: -- "$@"`
if [ $? != 0 ] ; then echo "Terminating..." >&2 ; exit 1 ; fi
# Note the quotes around `$TEMP': they are essential!
eval set -- "$TEMP"

eval set -- "$TEMP"
while [ $# -gt 0 ]
do
    case "$1" in
    (-d | --destdir) PKG_PATH="$2"; shift;;
    esac
    shift
done

echo "destdir = $PKG_PATH "
check_inst_right
check_files
mkdir -p $PKG_PATH/bin/
mkdir -p $PKG_PATH/etc/$PKG/conf
mkdir -p $PKG_PATH/var/$PKG/log

#we copy all file into opt/ dir and treat dir like normal unix arborescence
rsync -r --exclude .svn db/ $PKG_PATH/var/$PKG/db
if [ -d log ]; then rsync -r --exclude .svn log/ $PKG_PATH/var/$PKG/log; fi
rsync -r --exclude .svn conf/ $PKG_PATH/etc/$PKG/conf
rsync -r --exclude .svn npc/ $PKG_PATH/npc
cp athena-start $PKG_PATH/	
mv *-server* $PKG_PATH/bin/

ln -fs $PKG_PATH/var/$PKG/db/ $PKG_PATH/db
ln -fs $PKG_PATH/var/$PKG/log/ $PKG_PATH/log
ln -fs $PKG_PATH/etc/$PKG/conf/ $PKG_PATH/conf
ln -fs $PKG_PATH/athena-start /usr/bin/$PKG
for f in $(ls $PKG_PATH/bin/) ; do ln -fs $PKG_PATH/bin/$f $PKG_PATH/$f; done
echo "Installation is done you can now control server with '$PKG start'"
