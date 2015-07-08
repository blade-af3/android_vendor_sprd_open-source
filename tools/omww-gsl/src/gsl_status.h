/*********************************************************************************
 *
 * Copyright (c) 2015 OpenMobile Worldwide Inc.  All rights reserved.
 * Confidential and Proprietary to OpenMobile Worldwide, Inc.
 * This software is provided under nondisclosure agreement for evaluation purposes only.
 * Not for distribution.
 *
 *********************************************************************************/

#ifndef GSL_STATUS_H
#define GSL_STATUS_H

// GSL_VERSION must NOT contain a semicolon.
// This is used as a delimiter in the full-version string.
#define GSL_VERSION "1.0"

/*
 * Status codes to be used by the GSL and the ACL installer
 * for reporting messages to the user.  Codes are three
 * digits long; the first digit indicates the type of message,
 * and the next two specify the message itself.  This convention
 * is borrowed from HTTP 1.1
 *
 * The full list is as follows:
 *
 *   1xx - Informational
 *   2xx - Success
 *   4xx - error
 */


/* Information status */
#define INFO_INSTALL_START        100    /* Installation starting... */
#define INFO_INSTALL_PROGRESS1    101    /* Copying files... 20% */
#define INFO_INSTALL_PROGRESS2    102    /* Copying files... 40% */
#define INFO_INSTALL_PROGRESS3    103    /* for future use */
#define INFO_INSTALL_PROGRESS4    104    /* for future use */
#define INFO_INSTALL_DONE         105    /* Install completed... 100% */
#define INFO_UNINSTALL_START      106    /* Uninstall starting... */
#define INFO_UNINSTALL_DONE       107    /* Uninstall completed... 100% */
#define INFO_CREATE_BOM           108    /* Creating BOM */
#define INFO_UNWIND_UNINSTALL     109    /* Unwinding failed uninstall */
#define INFO_SAVING_PREVINSTALL   110    /* Saving previous installation */
#define INFO_REMOVE_BOM           111    /* Removing BOM */
#define INFO_REMOVE_BACKUP        112    /* Removing uninstall backup */
#define INFO_REMOVE_INSTALLER     113    /* Removing install script */
#define INFO_UPGRADE_UNINSTALL    114    /* Upgrade: Doing uninstall */
#define INFO_UPGRADE_INSTALL      115    /* Upgrade: Doing install */
#define INFO_PREVINSTALL_FAIL     116    /* Previous install failed */
#define INFO_UPGRADE_REVERTED     117    /* Upgrade failed, reverted to previous version */
#define INFO_UNPACKING_FILES      118    /* Unpacking files */
#define INFO_CHECKING_SPACE       119    /* Checking space */

/* Success status */
#define INSTALL_SUCCESS           200     /* Install: Success! */
#define UNINSTALL_SUCCESS         201     /* Uninstall: Success */

/* Error status */
#define ERR_GENERAL_ERROR         400     /* Unidentified error */
#define ERR_NO_DIRECTORY          401     /* Failed to create working directory */
#define ERR_NO_PACKAGE            402     /* Could not find package file */
#define ERR_VERIFY_FAIL           403     /* Package signature verification failed */
#define ERR_MARPATH_FAIL          404     /* Failed to build MAR path */
#define ERR_MARDIR_FAIL           405     /* Failed to create MAR directory */
#define ERR_MAREXTRACT_FAIL       406     /* Failed to extract the MAR package */
#define ERR_INSTALLSCRIPT_FAIL    407     /* Failed to run install script */
#define ERR_INSTALLER_NOTFOUND    408     /* Installer script not found */
#define ERR_UNINSTALLSCRIPT_FAIL  409     /* Failed to run uninstall script */
#define ERR_UNINSTALLER_NOTFOUND  410     /* Uninstall script not found */
#define ERR_ALREADY_INSTALLED     411     /* Existing installation detected, cancelling install */
#define ERR_INSTALL_FAILED        412     /* Install failed */
#define ERR_NOT_INSTALLED         413     /* Uninstall: package not installed */
#define ERR_CHECKPOINT_FAILED     414     /* Uninstall: checkpoint failed */
#define ERR_ALREADY_CURRENT       415     /* Upgrade: already at current version */
#define ERR_REMOUNT_RO_FAILURE    416     /* Cannot remount system read-only */
#define ERR_REMOUNT_RW_FAILURE    417     /* Cannot remount system read-write */
#define ERR_VENDOR_BIN_NOTFOUND   418     /* /vendor/bin directory not found */
#define ERR_INSTALLER_ARG_ERROR   419     /* Invalid arguments passed to installer */
#define ERR_INSTALL_RETRY_FAIL    420     /* Installation retry failed */
#define ERR_MULTIPLE_INSTALL_FAIL 421     /* Multiple installation failures */
#define ERR_INSUFFICIENT_SPACE    422     /* Not enough space to install ACL */

#define INSTALL_COMPLETE          888     /* Installation phase is done */

#endif /* GSL_STATUS_H */
