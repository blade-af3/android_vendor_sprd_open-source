                    OpenMobile World Wide Inc.
                  Generic Service Loader ("GSL")

GSL is designed to be installed on Firefox OS ("FxOS") version 2.0 and
above enabled devices to make those devices ACL-Ready. GSL is only to be
used in accordance with the included LICENSE "GSL Partner License".

Installation:

1. Compile and copy GSL binaries (acl-installer and acl-installer.sh)
onto the device (/system/bin/).
$ adb push acl-installer acl-installer.sh  /system/bin/

You can add the following lines to your product makefile so that GSL
binaries will get included in your FxOS build automatically.

PRODUCT_PACKAGES += \
    acl-installer       \
    acl-installer.sh    \
    acl_pubkey.der      \

2. Copy the public key (keys/acl_pubkey.der):
$ adb shell mkdir -p /system/etc/keys
$ adb push public_key.der /system/etc/keys/

3. Run GSL:
You need to either run GSL manually (for test only), or add it to init.rc.

To run GSL manually: from a root shell prompt on the FxOS device:

	# sh /system/bin/acl-installer.sh

To have GSL run automatically when the device boots, add the following
to /init.rc:

    service acl-installer /system/bin/sh /system/bin/acl-installer.sh
        class main
        user root


To check that the installer is running, use 'ps':
    # ps | grep acl-installer
    root      202   1     5592   2736  c025df90 b6f5e848 S acl-installer

You can also verify the log using 'adb logcat *:S INSTALLER:V'  You should
see a line of the form "INSTALLER version 1.0 running..." along with other
status.

NB: If GSL failed to start because of missing symbol error in NSS. Please
    export the missing by applying the provided patch:
$ git am -3 $PATH_OF_PATCH/NSS-VFY_EndWithSignature-for-libmar.patch

4. Install ACL
Launch the browser and enter the following URL:
https://marketplace.firefox.com/app/acl/ You might need to log into the
FxOS MarketPlace since this application is not published yet.

Once the webapp is installed, launch it, accept the EULA and navigate to
the install page. GSL will detect this and invoke the ACL installer. You
Will see the install progress within the app.

Un-installing ACL (long-tap, click "X") will cause GSL to remove these files.

If you have any questions or issues feel free to contact:
        Hamzata Diallo <hdiallo@openmobileww.com>
        Jim Paradis <jparadis@openmobileww.com>

