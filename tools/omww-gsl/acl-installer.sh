#!/system/bin/sh

if [ -z "$B2G_DIR" ]; then
  B2G_DIR="/system/b2g"
fi

export LD_LIBRARY_PATH=/vendor/lib:/system/lib:"$B2G_DIR"
export ACL_INSTALLER_STATUS_FILE=/storage/sdcard/acl_install.status

INSTALLSTATUS=`getprop persist.acl.install_status`
if [ "$INSTALLSTATUS" = "" ] ; then
    /system/bin/rm -f $ACL_INSTALLER_STATUS_FILE
fi

exec acl-installer $*

