#!/system/bin/sh

if [ -z "$B2G_DIR" ]; then
  B2G_DIR="/system/b2g"
fi

export LD_LIBRARY_PATH=/vendor/lib:/system/lib:"$B2G_DIR"

exec acl-installer $*

