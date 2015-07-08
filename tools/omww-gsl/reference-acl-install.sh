#!/system/bin/sh

BOM_DIRECTORY=/vendor/etc
BOM_FILE=$BOM_DIRECTORY/acl.bom
# INSTALL_BASE must have a trailing slash
INSTALL_BASE=/
BACKUP_SUFFIX=.aclbackup
INSTALL_SCRIPT=/vendor/bin/acl-install.sh
UNINSTALL_ARCHIVE_DIR=/data/local/.acl_uninstall
UNINSTALL_ARCHIVE_FILE=$UNINSTALL_ARCHIVE_DIR/acl-uninstall.tgz
UNINSTALL_RESTORED_BACKUPS_FILE=$UNINSTALL_ARCHIVE_DIR/acl-restored-backups
MANIFEST=/data/local/webapps/acl.openmobileww.com/manifest.webapp
PROP_ACL_VERSION="persist.acl.acl_version"

REPORTING_PORT=4567

CAT=/system/bin/cat
BUSYBOX=/vendor/bin/busybox

# Status codes for reporting
INFO_INSTALL_START=100
INFO_INSTALL_PROGRESS1=101
INFO_INSTALL_PROGRESS2=102
INFO_INSTALL_PROGRESS3=103
INFO_INSTALL_PROGRESS4=104
INFO_INSTALL_DONE=105
INFO_UNINSTALL_START=106
INFO_UNINSTALL_DONE=107
INFO_CREATE_BOM=108
INFO_UNWIND_UNINSTALL=109
INFO_SAVING_PREVINSTALL=110
INFO_REMOVE_BOM=111
INFO_REMOVE_BACKUP=112
INFO_REMOVE_INSTALLER=113
INFO_UPGRADE_UNINSTALL=114
INFO_UPGRADE_INSTALL=115
INSTALL_SUCCESS=200
UNINSTALL_SUCCESS=201
ERR_GENERAL_ERROR=400
ERR_NO_DIRECTORY=401
ERR_NO_PACKAGE=402
ERR_VERIFY_FAIL=403
ERR_MARPATH_FAIL=404
ERR_MARDIR_FAIL=405
ERR_MAREXTRACT_FAIL=406
ERR_INSTALLSCRIPT_FAIL=407
ERR_INSTALLER_NOTFOUND=408
ERR_UNINSTALLSCRIPT_FAIL=409
ERR_UNINSTALLER_NOTFOUND=410
ERR_ALREADY_INSTALLED=411
ERR_INSTALL_FAILED=412
ERR_NOT_INSTALLED=413
ERR_CHECKPOINT_FAILED=414
ERR_ALREADY_CURRENT=415
ERR_REMOUNT_RO_FAILURE=416
ERR_REMOUNT_RW_FAILURE=417
ERR_VENDOR_BIN_NOTFOUND=418
ERR_INSTALLER_ARG_ERROR=419

log() {
    [ -e $BUSYBOX ] && echo -n "$@" | $BUSYBOX nc 127.0.0.1 $REPORTING_PORT 2>/dev/null
    [ ! -z "$LOG" ] && echo "$@" >> $LOG
}

debugLog() {
    echo "$@"
    [ ! -z "$LOG" ] && echo "$@" >> $LOG
}

show_usage() {
    echo "Usage: $0 [ option ...]"
    echo "Options:"
    echo " --install <archive>"
    echo "   Installs from the specified archive"
    echo " --uninstall"
    echo "   Removes the existing installation"
    echo " --upgrade <archive>"
    echo "   Upgrades to the version specified in <archive>"
    echo " --installdir <path>"
    echo "   Specifies the directory in which the installation takes place"
    echo " --help"
    echo "   Print this text"
}

# Gets the version number from the manifest.  This implementation
# is rather fragile as it depends on a particular formatting of the
# version line.  To do it right we'd need a JSON parser.
get_manifest_version() {
    if [ ! -e $MANIFEST ] ; then
        MANIFEST_VERSION=
        return 1
    fi

    # MANIFEST_VERSION will already be set if the version were supplied
    # on the command line.  We don't need to go looking for it here.
    if [ "$MANIFEST_VERSION" = "" ] ; then
        MANIFEST_VERSION=`/system/bin/grep version $MANIFEST | $BUSYBOX awk -F\" ' { print $4 } '`
    fi
    return 0
}

# Checks the current ACL version (if any) against that in the
# manifest.
#    Returns 0 if the versions match
#    Returns 1 if the versions don't match, the manifest file
#        is missing, or the version property can't be found
check_version() {
    get_manifest_version
    if [ $? -ne 0 ] ; then
        return 1
    fi

    INSTALLED_VERSION=`/system/bin/getprop $PROP_ACL_VERSION`
    if [ "$INSTALLED_VERSION" = "" ] ; then
        return 1
    fi

    if [ "$MANIFEST_VERSION" = "$INSTALLED_VERSION" ] ; then
        return 0
    else
        return 1
    fi
}

# Sets the current ACL version property to that specified in the manifest
set_version() {
    get_manifest_version

    if [ $? -ne 0 ] ; then
        return 1
    fi

    /system/bin/setprop $PROP_ACL_VERSION "$MANIFEST_VERSION"
}

do_install() {

    # Copy this script and busybox to /vendor/bin so they'll be available
    debugLog "Starting install"

    # Copy this script to /vendor/bin so it'll be available
    # for uninstall whether the install succeeds or not.
    [ ! -e /vendor/bin ] && mkdir -p /vendor/bin

    if [ ! -e /vendor/bin ] ; then
        debugLog "do_install: /vendor/bin does not exist. Caller should have rectified this."
        log $ERR_VENDOR_BIN_NOTFOUND
        return 1
    fi

    /system/bin/cp $0 $INSTALL_SCRIPT

    # If there's no busybox already in /vendor/bin, then
    # copy over the one that's packed with this install
    [ ! -e $BUSYBOX ] && /system/bin/cp $INSTALL_DIR/busybox $BUSYBOX

    # See if there is an existing Bill of Materials
    if [ -e $BOM_FILE ] ; then
        log $ERR_ALREADY_INSTALLED
        return 1
    fi

    # Create a Bill of Materials (BOM)
    log $INFO_CREATE_BOM
    $CAT $ARCHIVE | $BUSYBOX tar tz | /system/bin/grep -v "/$" > $BOM_FILE

    # Look for any files that already exist and move them aside
    # if they're not there already
    for f in `$CAT $BOM_FILE` ; do
        if [ -e $INSTALL_BASE$f ] ; then
            if [ ! -e $INSTALL_BASE$f$BACKUP_SUFFIX ] ; then
                debugLog "Moving $f to $f$BACKUP_SUFFIX"
                /system/bin/mv $INSTALL_BASE$f $INSTALL_BASE$f$BACKUP_SUFFIX
            else
                debugLog "$f: backup detected, NOT copying"
            fi
        fi
    done

    # Extract the files
    $CAT $ARCHIVE | $BUSYBOX tar xz -C $INSTALL_BASE

    if [ $? -ne 0 ] ; then
        log $ERR_INSTALL_FAILED
        debugLog "Install failed"
        do_uninstall nosave
        return 1
    else
        log $INFO_INSTALL_DONE
        debugLog "Install succeeded"
        set_version
        return 0
    fi

}

unwind_uninstall() {
    # Unwind a failed uninstall.
    # $UNINSTALL_RESTORED_BACKUPS_FILE contains the names
    # of any files that were successfully renamed from .aclbackup
    # back to their original names.  We need to reverse
    # this before we can restore the tar

    log $INFO_UNWIND_UNINSTALL
    if [ -e $UNINSTALL_RESTORED_BACKUPS_FILE ] ; then
        for f in `$CAT $UNINSTALL_RESTORED_BACKUPS_FILE` ; do
            debugLog "Moving $INSTALL_BASE$f to $INSTALL_BASE$f$BACKUP_SUFFIX"
            /system/bin/mv $INSTALL_BASE$f $INSTALL_BASE$f$BACKUP_SUFFIX
        done
    fi

    # Unpack the saved tarball
    $CAT $UNINSTALL_ARCHIVE_FILE | $BUSYBOX tar xz -C $INSTALL_BASE

}

do_uninstall() {
    # TODO: Make sure Zygote is not running

    log $INFO_UNINSTALL_START
    debugLog "Starting uninstall"

    # By default, we squirrel away information about the
    # existing installation so we can unwind the uninstall
    # if it fails.  If we're called with the "nosave"
    # argument then we don't do this.
    if [ "$1" = "nosave" ] ; then
        SAVE=
    else
        SAVE=YES
    fi


    SAVE_FAILED=
    if [ "$SAVE" = "YES" ] ; then
        log $INFO_SAVING_PREVINSTALL
        /system/bin/mkdir $UNINSTALL_ARCHIVE_DIR
        $BUSYBOX tar cz -C $INSTALL_BASE `$CAT $BOM_FILE` > $UNINSTALL_ARCHIVE_FILE
        if [ $? -ne 0 ] ; then
            SAVE_FAILED=TRUE
        fi
        touch $UNINSTALL_RESTORED_BACKUPS_FILE
        if [ $? -ne 0 ] ; then
            SAVE_FAILED=TRUE
        fi
        if [ "$SAVE_FAILED" = "TRUE" ] ; then
            log $ERR_CHECKPOINT_FAILED
            /system/bin/rm $UNINSTALL_ARCHIVE_FILE
            /system/bin/rm $UNINSTALL_RESTORED_BACKUPS_FILE
            /system/bin/rm -r $UNINSTALL_ARCHIVE_DIR
        fi
    fi

    # Go through the BOM.  If a file has a backup
    # version then mv it back, otherwise rm the file
    if [ ! -e $BOM_FILE ] ; then
        log $ERR_NOT_INSTALLED
        return 1
    fi

    RESTORED_BACKUPS=

    for f in `$CAT $BOM_FILE` ; do
        if [ -e $INSTALL_BASE$f$BACKUP_SUFFIX ] ; then
            # Backup version detected, move it back
            FN=$INSTALL_BASE$f$BACKUP_SUFFIX
            DN=`$BUSYBOX dirname $FN`
            BN=`$BUSYBOX basename $FN $BACKUP_SUFFIX`
            debugLog "Moving $FN back to $DN/$BN"
            mv $FN $DN/$BN
            if [ $? -ne 0 ] ; then
                if ["$SAVE" = "YES" ] ; then
                    unwind_uninstall
                fi
                return 1
            fi
            if [ "$SAVE" = "YES" ] ; then
                echo $DN/$BN >> $UNINSTALL_RESTORED_BACKUPS_FILE
            fi
        else
            # It's a file we installed; remove it
            debugLog "Removing $INSTALL_BASE$f"
            /system/bin/rm $INSTALL_BASE$f
            if [ $? -ne 0 ] ; then
                if ["$SAVE" = "YES" ] ; then
                    unwind_uninstall
                fi
                return 1
            fi
        fi
    done

    # Finally, remove the BOM
    log $INFO_REMOVE_BOM
    /system/bin/rm $BOM_FILE

    if [ "$SAVE" = "YES" ] ; then
        log $INFO_REMOVE_BACKUP
        /system/bin/rm $UNINSTALL_ARCHIVE_FILE
        /system/bin/rm -r $UNINSTALL_ARCHIVE_DIR
    fi

    log $INFO_REMOVE_INSTALLER
    /system/bin/rm $INSTALL_SCRIPT
    /system/bin/rm $BUSYBOX

    setprop $PROP_ACL_VERSION ""

    log $INFO_UNINSTALL_DONE
    debugLog "Uninstall done"
    return 0
}

do_upgrade() {
    # Upgrade by doing an uninstall with save.
    # If that succeeds, then do an install
    # If the uninstall fails, it will unwind itself
    # and return an error

    debugLog "Upgrade start"

    # Check version to see if we need to upgrade
    check_version
    if [ $? -eq 0 ] ; then
        CURRENTVERSION=`/system/bin/getprop $PROP_ACL_VERSION`
        log $ERR_ALREADY_CURRENT
        debugLog "ACL already current, not updating"
        return 1
    fi

    log $INFO_UPGRADE_UNINSTALL
    do_uninstall save
    if [ $? -eq 0 ] ; then
        log $INFO_UPGRADE_INSTALL
        do_install
    fi

    debugLog "Upgrade done"
}

debugLog "Running installation script at $0 $*"
debugLog "   in directory `pwd`"

# Mount filesystem read/write so we can install files
mount -o remount,rw /system
if [ $? -ne 0 ]; then
    log $ERR_REMOUNT_RW_FAILURE
    exit 1
fi

OPERATION=
ARCHIVE=
INSTALL_DIR=
MANIFEST_VERSION=

# Parse args

while [ $# -gt 0 ] ; do
    case $1 in
        --install | --upgrade)
            if [ "$OPERATION" = "" ] ; then
                OPERATION=$1
                ARCHIVE=$2
                shift 2
            else
                log $ERR_INSTALLER_ARG_ERROR
                exit 1
            fi
            ;;
        "--uninstall")
            if [ "$OPERATION" = "" ] ; then
                OPERATION=$1
                shift
            else
                log $ERR_INSTALLER_ARG_ERROR
                exit 1
            fi
            ;;
        "--installdir")
            INSTALL_DIR=$2
            shift 2
            ;;
        "--version")
            MANIFEST_VERSION=$2
            shift 2
            ;;
        "--help")
            show_usage
            exit 1
            ;;
        *)
            log $ERR_INSTALLER_ARG_ERROR
            debugLog "Unrecognized option $1"
            show_usage
            exit 1
            ;;
    esac
done

case $OPERATION in
    "--install")
        do_install
        ;;
    "--uninstall")
        do_uninstall
        ;;
    "--upgrade")
        do_upgrade
        ;;
    *)
        log $ERR_INSTALLER_ARG_ERROR
        exit 1
        ;;
esac

# Revert to readonly
# We have to do this out of a script on a different filesystem because
# we may have actually deleted this script, leaving us with
# a dangling reference that will prevent remount

export REMOUNT_SCRIPT=/data/local/tmp/do_remount
cat << EOF > $REMOUNT_SCRIPT
#!/system/bin/sh
set -x
/system/bin/mount -o remount,ro /system
/system/bin/rm $REMOUNT_SCRIPT
exit 0
EOF

/system/bin/chmod 777 $REMOUNT_SCRIPT
exec /system/bin/sh $REMOUNT_SCRIPT

