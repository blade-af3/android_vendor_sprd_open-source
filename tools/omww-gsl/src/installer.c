/*********************************************************************************
 *
 * Copyright (c) 2014 OpenMobile Worldwide Inc.  All rights reserved.
 * Confidential and Proprietary to OpenMobile Worldwide, Inc.
 * This software is provided under nondisclosure agreement for evaluation purposes only.
 * Not for distribution.
 *
 *********************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include "unzip.h"

#include "mar_utils.h"
#include "gsl_status.h"

#if !defined(OPEN_MAX)
#define OPEN_MAX (256)
#endif

#define VERSION "\"version\""
#define ACL_APP_ID "acl.openmobileww.com"
#define MANIFEST "manifest.webapp"
#define SURFACEFLINGER "init.svc.surfaceflinger"
#define ZYGOTE "init.svc.zygote"

#define PROP_INSTALL_STATUS "persist.acl.install_status"
#define PROP_GSL_VERSION    "persist.acl.gsl.version"
#define PROP_ACL_VERSION    "persist.acl.acl_version"
#define PROP_ACL_EXT_VERSION "persist.acl.version"
#define PROP_PRODUCT_NAME    "ro.product.name"
#define PROP_PRODUCT_DEVICE  "ro.product.device"
#define PROP_PRODUCT_MODEL   "ro.product.model"

#define  INSTALL_STATUS_INSTALLED       "installed"
#define  INSTALL_STATUS_UNINSTALLED     "uninstalled"
#define  INSTALL_STATUS_INSTALLING      "installing"
#define  INSTALL_STATUS_UNINSTALLING    "uninstalling"
#define  INSTALL_STATUS_UPGRADING       "upgrading"
#define  INSTALL_STATUS_FAILED          "failed"
#define  INSTALL_STATUS_RETRYFAIL       "retryfail"

#define SYS_CMD_MAX_SIZE     1024
#define MAX_LINE_SIZE 256

#define EVENT_SIZE      ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN   ( 5* ( EVENT_SIZE + NAME_MAX ) )

/* Default values for module variables that can
   be overwritten with command line arguments. */
#define DEFAULT_STATUS_REPORT_PORT 4567

#define DEFAULT_WEBAPP_DIR         "/data/local/webapps"
#define DEFAULT_TEMP_DIR           "/data/local/.installer"

#if DBG
#define DEFAULT_VERBOSE            1
#else
#define DEFAULT_VERBOSE            0
#endif

#define DEFAULT_PKG_FILE_PATTERN   "*.mar"
#define DEFAULT_KEY_DIR            "/system/etc/keys"
#define DEFAULT_TAR_FILE_PATTERN   "*.tgz"

#define MOUNT_RETRY_COUNT          5
#define MAR_EXTRACT_SUBDIR         "mar_contents"

#define INSTALLER_SCRIPT           "acl-install.sh"
#define INSTALLER_SCRIPT_DIRECTORY "/vendor/bin"

#define INSTALLER_SUCCESS           0
#define INSTALLER_FAILURE           1
#define INSTALLER_REVERTED          2

#define DEFAULT_INSTALLER_STATUS_FILE      "/storage/sdcard/acl_install.status"

#define APPLICATION_ZIP_FILE       "application.zip"

/* The following variables can be modified by command line parameters. */

/* Package file to install. */
static char * pkg_file              = NULL;

/* Buffer to store name of package file if it found by this module. */
static char pkg_file_buffer[MAXPATHLEN];

/* Port number from which the installer service will
   report status messages. */
unsigned short status_report_port   = DEFAULT_STATUS_REPORT_PORT;

/* Temporary directory that will be created to extract
   application files prior to installation. */
static char * temp_dir              = DEFAULT_TEMP_DIR;

/* File pattern (eg. extension) for package archive file
   within the application's .zip file that will be extracted
   in the install. */
static char * pkg_file_pattern      = DEFAULT_PKG_FILE_PATTERN;

/* Flag that enables verbose debug messages. */
static int    verbose               = DEFAULT_VERBOSE;

/* Directory containing public keys used to verify MAR files. */
static char * key_dir               = DEFAULT_KEY_DIR;
/* Directory containing the NSS database files. */
static char * nssdb_dir             = NULL;
/* File pattern (eg. extension) for tarfile within the MAR
   archive file that will be extracted in the install. */
static char * tar_file_pattern      = DEFAULT_TAR_FILE_PATTERN;

/* Report a status message to a listening socket open at port: status_report_port. */

static void reportStatus (int status) {
    int sock = -1;
    struct sockaddr_in addr;
    char message[MAX_LINE_SIZE];

    snprintf(message, sizeof(message), "%d", status);

    ALOGD_IF(verbose, "reportStatus: '%s'", message);

    sock = socket (AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    memset (&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(status_report_port);
    if (!inet_aton("127.0.0.1", &(addr.sin_addr))) goto failure;

    if (connect(sock, (struct sockaddr *)(&addr), sizeof(addr))) goto failure;

    write(sock, message, strlen (message));

failure:
    close(sock);
}

// Replace all instances of 'c1' in string 's' with 'c2'
static void replace_char(char *s, char c1, char c2)
{
    int i;
    for(i = 0; s[i]; i++) {
        if (s[i] == c1) {
            s[i] = c2;
        }
    }
}

static void update_properties() {
    char device[PROPERTY_VALUE_MAX] = {'\0'};
    char model[PROPERTY_VALUE_MAX] = {'\0'};
    char product[PROPERTY_VALUE_MAX] = {'\0'};
    char aclversion[PROPERTY_VALUE_MAX] = {'\0'};
    char fullversion[PROPERTY_VALUE_MAX] = {'\0'};

    property_set(PROP_GSL_VERSION, GSL_VERSION);

    property_get(PROP_PRODUCT_NAME,   product, NULL);
    property_get(PROP_PRODUCT_DEVICE, device, NULL);
    property_get(PROP_PRODUCT_MODEL,  model, NULL);
    property_get(PROP_ACL_VERSION, aclversion, NULL);
    replace_char(product, ';', ' ');
    replace_char(device, ';', ' ');
    replace_char(model, ';', ' ');
    replace_char(aclversion, ';', ' ');

    // FORMAT: <DEVICE>;<PRODUCT>;<MODEL>;<GSL_VERSION>;<ACL_VERSION>
    // Firefox MarketPlace will read and parse this string for device
    // filtering. Please do NOT change this.
    // NOTE: ACL version might be empty if ACL is not installed.
    if (0 < snprintf(fullversion, sizeof(fullversion), "%s;%s;%s;%s;%s",
                device, product, model, GSL_VERSION, aclversion)) {
        ALOGD_IF(verbose, "Setting property '%s' to '%s'\n",
                PROP_ACL_EXT_VERSION, fullversion);
        property_set(PROP_ACL_EXT_VERSION, fullversion);
    }
}

/* Return TRUE if the user has accepted the EULA, FALSE otherwise.
 * Currently this is only used on fresh installs, not uninstall
 * or upgrade.  In this case, if the user is at the interface
 * screen then we know they've gone through the EULA part.
 * Therefore, if we can talk to the interface screen then we know
 * we're good.  A future implementaiton might use persistent
 * properties instead.
 */
static bool is_eula_accepted() {
    int sock = -1;
    struct sockaddr_in addr;
    bool ret = false;

    sock = socket (AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    memset (&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(status_report_port);
    if (!inet_aton("127.0.0.1", &(addr.sin_addr))) goto failure;

    if (connect(sock, (struct sockaddr *)(&addr), sizeof(addr))) goto failure;

    ret = true;
failure:
    close(sock);
    return ret;
}

/* Close any files that this process has open except for
 * stdin, stdout, or stderr
 */
static int close_open_fds() {
    int fd, max_fd;

    ALOGD_IF(verbose, "Closing file descriptors\n");
    if ((max_fd = getdtablesize()) == -1) {
        max_fd = OPEN_MAX;
    }

    for (fd = max_fd; fd >= 0; fd--) {
        if ((fd != fileno(stdin)) &&
                (fd != fileno(stdout)) &&
                (fd != fileno(stderr))) {
            close(fd);
        }
    }

    return 0;
}

// Reset all signal handlers to default values
static int reset_signal_handlers()
{
   int i;
   struct sigaction sa;

   memset(&sa, 0, sizeof(struct sigaction));
   sigemptyset(&sa.sa_mask);
   sa.sa_handler = SIG_DFL;

   for(i = 0; i < NSIG; i++) {
        sigaction(i, &sa, NULL);
   }
   return 0;
}

static void reopen_devnull_stdio(FILE * stream) {
    struct stat st;
    int fd;

    fd = fileno(stream);
    if ((fstat(fd, &st) == -1) && (errno == EBADF)) {
        freopen(_PATH_DEVNULL, "rb", stream);
    }
}

/* Issue a safe system command with fork and exec. */
static int safeSystem (const char * exec_path, ...) {
    int ret = -1;
    int status = 0;
    pid_t pid;
    va_list args;
    int argc, i;
    char ** argv = NULL;
    char *envp[] = {
        "PATH=" _PATH_STDPATH,
        "IFS= \t\n",
        NULL
    };
    sigset_t orig, mask;

    if (NULL == exec_path) {
        goto failure;
    }
    /* Get argument count. */
    va_start (args, exec_path);
    argc = 1;
    while (va_arg (args, const char *) != NULL) {
        argc++;
    }
    va_end (args);

    /* Allocate argument vector. */
    argv = malloc ((argc+1) * sizeof(const char *));
    if (NULL == argv) {
        goto failure;
    }

    /* Build argument vector. */
    va_start (args, exec_path);
    argv[0] = (char*) exec_path;
    for (i = 1; i < argc; i++) {
        argv[i] = (char*) va_arg (args, const char *);
    }
    va_end (args);
    argv[argc] = NULL;

    /* Block all signals so we don't get interrupted
     * while setting up the child process
     */
    sigfillset(&mask);
    sigprocmask(SIG_SETMASK, &mask, &orig);

    /* Fork and run executable. */
    pid = fork ();
    if (pid == 0) {

        // Close open fds and ensure that only the
        // minimally-required environment variables are
        // available to the executable.
        close_open_fds();
        reopen_devnull_stdio(stdin);
        reopen_devnull_stdio(stdout);
        reopen_devnull_stdio(stderr);

        // Reset signal handlers to default
        reset_signal_handlers();

        // Unblock all signals just before execve
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        execve (exec_path, argv, envp);
        _exit(127);
    }
    else {
        // Put the sigmask back to where it was before fork
        sigprocmask(SIG_SETMASK, &orig, NULL);

        if (pid == -1) {
            ALOGD_IF(verbose, "safeSystem: fork failed: %s\n", strerror(errno));
        }
        else {
            waitpid (pid, &status, 0);
            if (WIFEXITED (status)) {
                ret = WEXITSTATUS (status);
            }
            else if (WIFSIGNALED(status)) {
                ALOGD_IF(verbose, "safeSystem: child terminated with signal %d\n", WTERMSIG(status));
            }
        }
    }

failure:
    if (argv) free (argv);

    return ret;
}

static char* getVersionFromManifest(char *version, int maxsize) {
    char * ret = NULL;
    memset(version, 0, maxsize);
    FILE* fp = fopen(DEFAULT_WEBAPP_DIR"/"ACL_APP_ID"/"MANIFEST, "r");
    if (fp != NULL) {
        char line [MAX_LINE_SIZE] = {0};
        char *p = NULL;

        while (fgets(line, sizeof (line), fp)) {
            if (NULL != (p = strstr(line, VERSION))) {
                p += strlen(VERSION);
                p = strchr(p, '"');
                if (p == NULL) continue;
                p++;
                char *aclVersion = strsep(&p, "\"");
                if (p != NULL) {
                    ret = strncpy(version, aclVersion, maxsize-1);
                    break;
                }
            }
        }
        fclose(fp);
    }

    return ret;
}

/* Unzip the contents of the installer's application.zip. */
static int unzipApp (const char * dir, const char * app_id, const char * dest_dir) {
    int ret = -1, retry = 10;
    int result;
    char zipfile[MAXPATHLEN];

    if (NULL == dir || NULL == app_id || NULL == dest_dir) {
        goto failure;
    }

    result = snprintf (zipfile, sizeof(zipfile), "%s/%s/%s",
            dir, app_id, APPLICATION_ZIP_FILE);
    if (result < 0 || result >= (int)sizeof(zipfile)) {
        goto failure;
    }

    while(0 != (ret = access(zipfile, 0)) && retry-- > 0) {
        sleep(1);
    }

    if (0 != ret) {
        ALOGD("zipfile not found");
        goto failure;
    }

    if(0 != unzip(zipfile, dest_dir)) {
        ALOGD("file unzip failed");
        goto failure;
    }

    ALOGD("%s is extracted to '%s'\n", zipfile, dest_dir);
    ret = 0;

failure:
    return ret;
}

bool needToUpdate() {
    char manifest_version[MAX_LINE_SIZE] = {'\0'};
    char current_version[PROPERTY_VALUE_MAX] = {'\0'};

    if (getVersionFromManifest(manifest_version,
                sizeof(manifest_version)) != NULL) {
        property_get(PROP_ACL_VERSION, current_version, NULL);

        if (strlen(current_version) == 0) {
            ALOGD("ACL is not installed\n");
            return false;
        }

        if (strcmp(manifest_version, current_version) == 0) {
            ALOGD("ACL version %s is up to date", current_version);
            return false;
        }

        ALOGD("ACL version %s needs to be upgraded to %s", current_version,
                manifest_version);
    }
    else {
        ALOGD("Unable to read ACL version from the manifest, "
                "assuming upgrade needed...");
    }

    return true;
}

/* Build the path to the subdirectory where the contents of the MAR file will be extracted. */
static int getMarDir (const char * parent_dir,
                      char * dirname_buff,
                      size_t dirname_buff_size) {
    int ret = -1;

    if (parent_dir && dirname_buff) {
        ret = snprintf (dirname_buff, dirname_buff_size, "%s/" MAR_EXTRACT_SUBDIR, parent_dir);
        if (ret < 0 || ret >= (int) dirname_buff_size) {
            ret = -1;
        }
    }

    return ret;
}

/* Find package file matching pkg_file_pattern in parent_dir. */
static int locatePackagefile (const char * parent_dir,
                              char * filename_buff,
                              size_t filename_buffer_size) {
    int result = -1;
    char syscmd[SYS_CMD_MAX_SIZE];
    FILE *fp;
    size_t nl;

    /* Look for package file with matching patter in the working directory. */
    if (parent_dir && filename_buff) {
        if (snprintf (syscmd, sizeof(syscmd), "/system/bin/ls %s/%s",
                    parent_dir, pkg_file_pattern) < (int)sizeof(syscmd)) {
            fp = popen (syscmd, "r");
            if (fp) {
                /* Get stdout from ls command. */
                if (fgets (filename_buff, filename_buffer_size, fp) != NULL) {
                    nl = strlen(filename_buff);
                    if (nl > 0 && filename_buff[nl-1] == '\n') {
                        filename_buff[nl-1] = '\0';
                        result = 0;
                    }
                }
                pclose(fp);
            }
        }
    }

    return result;
}

/* Recursively remove a directory if it exists. */
static int cleanupDir(const char *dir) {
    int ret = 0;
    struct stat file_info;

    if (stat(dir, &file_info) == 0) {
        safeSystem("/system/bin/rm", "-r", dir, NULL);
        if (stat(dir, &file_info) == 0) {
            ret = -1;
        }
    }

    return ret;
}

/* Create a new directory. If one already exists, remove it. */
static int createCleanDirectory(const char *dir) {
    int ret = -1;
    /* Remove existing directory. */
    if (cleanupDir(dir) == 0) {

        /* Create new directory. */
        if (mkdir(dir, 0700) == 0) {
            ret = 0;
        }
    }

    return ret;
}

/* Remount a filesystem (used to remount system partition as rw and ro). */
static int doMount(const char *src, const char *dest, const char *fstype,
        unsigned long flags, const void *data) {

    int ret = -1, retry = MOUNT_RETRY_COUNT;

    for (; ret < 0 && retry > 0; retry--) {
        ret = mount(src, dest, fstype, flags, data);
        if (ret < 0) sleep(1);
    }
    return ret;
}

/* Set the install status in both the properties (read by GSL/ACL)
 * and the install status file (read by ACL Webapp)
 */
static void setInstallStatus(char * status)
{
    FILE * statusFile;
    char * statusFilePath;

    statusFilePath = getenv("ACL_INSTALLER_STATUS_FILE");
    if(!statusFilePath) {
        statusFilePath = DEFAULT_INSTALLER_STATUS_FILE;
    }

    ALOGD("Setting installation status to \"%s\"\n", status);
    statusFile = fopen(statusFilePath, "w");
    if(statusFile) {
        fputs(status, statusFile);
        fclose(statusFile);
    }
    else {
        ALOGD("Unable to write status '%s' to status file %s\n", status, statusFilePath);
    }

    property_set(PROP_INSTALL_STATUS, status);
}

/* Start installation process. */
static int install (bool do_upgrade) {
    int cleanup_install = 0;
    int ret = -1;
    char mar_dir_buff[MAXPATHLEN];
    char install_script_path[MAXPATHLEN];
    char tar_archive_path[MAXPATHLEN];
    char manifest_version[MAX_LINE_SIZE];

    char* installer_option = do_upgrade ? "--upgrade" : "--install";

    setInstallStatus(do_upgrade ? INSTALL_STATUS_UPGRADING : INSTALL_STATUS_INSTALLING);

    ALOGD ("---- Start Installation ------\n");

    reportStatus (INFO_INSTALL_START);

    /* Create directory to unzip application.zip. */
    if (createCleanDirectory(temp_dir) < 0) {
        reportStatus (ERR_NO_DIRECTORY);
        ALOGE("----- Failed to create working directory! -----\n");
        goto failure;
    }

    /* Unzip application.zip. */
    if (unzipApp (DEFAULT_WEBAPP_DIR, ACL_APP_ID, temp_dir) < 0) {
        ALOGE_IF (verbose, "----- Error unzipping app with id: "
                "'%s' to '%s' !!! -----\n", ACL_APP_ID, temp_dir);
        goto failure;
    }

    /* Locate the package file in the unzipped contents. */
    if (locatePackagefile (temp_dir, pkg_file_buffer, sizeof(pkg_file_buffer)) < 0) {
        reportStatus (ERR_NO_PACKAGE);
        ALOGE_IF (verbose, "----- Could not find package file!!! -----\n");
        goto failure;
    }
    pkg_file = pkg_file_buffer;

    /* Verify the MAR file's signature. */
    ALOGD_IF (verbose, "Verifying MAR file...\n");
    if (MU_verifyMarfile (pkg_file, key_dir, verbose) < 0) {
        reportStatus (ERR_VERIFY_FAIL);
        ALOGE_IF (verbose, "----- Error verifying MAR file!!! -----\n");
        goto failure;
    }

    /* Get the path to where the MAR file is to be extracted. */
    if (getMarDir (temp_dir, mar_dir_buff, sizeof(mar_dir_buff)) < 0) {
        reportStatus (ERR_MARPATH_FAIL);
        ALOGE_IF (verbose, "Failed to build MAR path!\n");
        goto failure;
    }

    /* Create MAR directory. */
    if (createCleanDirectory(mar_dir_buff) < 0) {
        reportStatus (ERR_MARDIR_FAIL);
        ALOGE_IF (verbose, "Failed to create MAR directory!\n");
        goto failure;
    }

    reportStatus (INFO_INSTALL_PROGRESS1);

    /* Extract MAR file. */
    ALOGD_IF (verbose, "Extracting MAR file...\n");
    if (MU_extractMarfile (pkg_file, mar_dir_buff) < 0) {
        reportStatus (ERR_MAREXTRACT_FAIL);
        ALOGE_IF (verbose, "Error extracting MAR file!\n");
        goto failure;
    }

    reportStatus (INFO_INSTALL_PROGRESS2);

    /* See if the MAR file included its own installer script */
    snprintf(install_script_path, sizeof(install_script_path), "%s/%s",
            mar_dir_buff, INSTALLER_SCRIPT);
    snprintf(tar_archive_path, sizeof(tar_archive_path), "%s/%s", mar_dir_buff, tar_file_pattern);

    if (0 == access(install_script_path, 0)) {
        ALOGD("+++++++ Using installer script at %s", install_script_path);
        if (getVersionFromManifest(manifest_version, sizeof(manifest_version)) == NULL) {
            ret = safeSystem("/system/bin/sh", install_script_path, installer_option,
                             tar_archive_path, "--installdir", mar_dir_buff, NULL);
        }
        else {
            ret = safeSystem("/system/bin/sh", install_script_path, installer_option,
                             tar_archive_path, "--installdir", mar_dir_buff,
                             "--version", manifest_version, NULL);
        }
        ALOGD_IF (verbose, "safeSystem returns %d\n", ret);
        if (ret == INSTALLER_REVERTED) {
            reportStatus (INFO_UPGRADE_REVERTED);
            ALOGE_IF (verbose, "Upgrade failed, successfully reverted to previous version\n");
            cleanup_install = 1;
            goto reverted;
        }
        else if (ret != 0) {
            reportStatus (ERR_INSTALLSCRIPT_FAIL);
            ALOGE_IF (verbose, "Error running install script!\n");
            cleanup_install = 1;
            goto failure;
        }
    } else {
        reportStatus(ERR_INSTALLER_NOTFOUND);
        ALOGE_IF (verbose, "Installer script not found\n");
        cleanup_install = 1;
        goto failure;
    }
    if (do_upgrade)
        ALOGD ("----- ACL UPGRADED -----\n");
    else
        ALOGD ("----- ACL INSTALLED -----\n");

    reportStatus (INFO_INSTALL_DONE);
    reportStatus (INSTALL_SUCCESS);
    reportStatus (INSTALL_COMPLETE);

reverted:
    setInstallStatus(INSTALL_STATUS_INSTALLED);

    /* Cleanup. */
    cleanupDir(temp_dir);

    // Update the properties in case ACL version has changed
    update_properties();

    return 0;

failure:
    setInstallStatus(INSTALL_STATUS_FAILED);
    /* On failure, bin file was not run, clean up state of the filesystem */
    cleanupDir(temp_dir);

    if (do_upgrade)
        ALOGE ("----- ACL UPGRADE FAILED! -----\n");
    else
        ALOGE ("----- ACL INSTALLATION FAILED! -----\n");

    // Update the properties in case ACL version has changed
    update_properties();

    return -1;
}

static int uninstall () {
    int cleanup_install = 0;
    char install_script_path[MAXPATHLEN];

    setInstallStatus(INSTALL_STATUS_UNINSTALLING);

    ALOGD ("---- Start Uninstall ------\n");
    reportStatus (INFO_UNINSTALL_START);

    snprintf(install_script_path, sizeof(install_script_path), "%s/%s",
            INSTALLER_SCRIPT_DIRECTORY, INSTALLER_SCRIPT);

    if (0 == access(install_script_path, 0)) {
        ALOGD("+++++++ Using install/uninstall script at %s", install_script_path);
        if (safeSystem ("/system/bin/sh", install_script_path, "--uninstall", NULL) != 0) {
            reportStatus (ERR_UNINSTALLSCRIPT_FAIL);
            ALOGE_IF (verbose, "Error running uninstall script!\n");
            cleanup_install = 1;
            goto failure;
        }
    } else {
        // If there's no uninstall script then likely the install didn't
        // get very far (putting this in place is one of the first things
        // the installer does).  Treat this as evidence that the install
        // never really took place.  Note what happened but don't fail
        // the operation
        reportStatus(ERR_UNINSTALLER_NOTFOUND);
        ALOGE_IF (verbose, "Uninstaller script not found\n");
    }

    ALOGD ("----- UNINSTALLED -----\n");
    reportStatus (INFO_UNINSTALL_DONE);
    reportStatus (UNINSTALL_SUCCESS);

    setInstallStatus(INSTALL_STATUS_UNINSTALLED);

    // Update the properties in case ACL version has changed
    update_properties();

    return 0;

failure:
    setInstallStatus(INSTALL_STATUS_FAILED);

    ALOGE ("----- UNINSTALLATION FAILED! -----\n");

    // Update the properties in case ACL version has changed
    update_properties();

    return -1;
}

static bool isACLPresent() {
    struct stat st;
    return 0 == stat(DEFAULT_WEBAPP_DIR"/"ACL_APP_ID, &st);
}

static bool isACLRunning() {
    char zygoteStatus[PROPERTY_VALUE_MAX] = {'\0'};
    char surfaceflingerStatus[PROPERTY_VALUE_MAX] = {'\0'};

    if ((property_get(ZYGOTE, zygoteStatus, NULL) && strcmp(zygoteStatus, "running") == 0) &&
        (property_get(SURFACEFLINGER, surfaceflingerStatus, NULL) && strcmp(surfaceflingerStatus, "running") == 0))
        return true;
    else
        return false;
}


/*****************************************************************************
 * Function: doUpdate()
 *
 * Description: Update to the newest version ACL
 *
 * Return: 0 = success  -1 = failure
 *****************************************************************************/
static int doUpdate(int force_update) {
     int ret;

     if (!force_update && !needToUpdate())
         return 0;

     ALOGD("Updating ACL...");

     ret = install(true);

     return ret;
}

/*****************************************************************************
 * Function: doUninstall()
 *
 * Description: Uninstall ACL
 *
 * Return: 0 = success  -1 = failure
 *****************************************************************************/
static int doUninstall() {
     ALOGD("Uninstall ACL...");
     int ret = uninstall();

     return ret;
}

/*****************************************************************************
 * Function: doInstall()
 *
 * Description: if it is ACL app, install it
 *
 * Return: void
 *****************************************************************************/
static int doInstall() {
     ALOGD("Waiting for EULA check\n");
     while (!is_eula_accepted()) {
        // If ACL disappeared while we were waiting,
        // don't do anything else
        if (!isACLPresent()) {
            return 0;
        }
        sleep(1);
     }
     ALOGD("Install ACL...");
     int ret = install(false);

     return ret;
}

/*****************************************************************************
 * Function: handleInotifyEvents()
 *
 * Description: function called when Inotify events are available
 *
 * Params: appInstaller
 *
 * Return: 0 for success and -1 for failure
 *****************************************************************************/
static int handleInotifyEvents(int fd, int inotifyWd, int inotifyACLWd) {
    int length, i, ret = 0;
    char buffer[EVENT_BUF_LEN] = {0};

    length = read( fd, buffer, EVENT_BUF_LEN);
    if ( length > 0 ) {
        i = 0;
        while ( i < length ) {
            struct inotify_event *event = (struct inotify_event * ) &buffer[i];
            if (event != NULL) {
                ALOGD_IF (verbose, "-- handleInotifyEvents event->name='%s' fd=%d event->mask=0x%x"
                        " len=%d, offset=%d, total length=%d", event->name, event->wd,
                        event->mask, event->len, i, length);

                i += EVENT_SIZE + event->len;

                if (event->len && (event->wd == inotifyWd) && (event->mask & IN_ISDIR)) {
                    if (strcmp(event->name, ACL_APP_ID) == 0) {
                        if (event->mask & (IN_CREATE|IN_MOVED_TO)) {
                            ret = doInstall();
                        }
                        else if ((event->mask & IN_DELETE) && !isACLPresent()) {
                            ret = doUninstall();
                        }
                        else if ((event->mask & IN_MODIFY) && isACLPresent()){
                            ret = doUpdate(false);
                        }
                    }
                }
                else if(event->len && (event->wd == inotifyACLWd)) {
                    /* Event is in the application's directory itself, so it's an update */
                    if ( isACLPresent() &&
                            (strcmp(event->name, APPLICATION_ZIP_FILE) == 0) &&
                            (event->mask & (IN_CREATE|IN_MOVED_TO|IN_MODIFY)) ) {
                        ret = doUpdate(false);
                    }
                }
            }
        }
        return ret;
    }
    return -1;
}


static int run() {
    int ret = 0;
    fd_set set_fds, read_fds;
    int fd, max_fd = 0;
    int inotifyFd, inotifyWd= 0;
    int inotifyACLWd = -1;
    int nb_ready = 0;

    // Initialize inotify
    inotifyFd = inotify_init();
    if ( inotifyFd < 0 ) {
        ALOGE("inotify_init failed, error: '%s'", strerror(errno));
        return -1;
    }

    inotifyWd = inotify_add_watch(inotifyFd, DEFAULT_WEBAPP_DIR,
            IN_MOVED_TO | IN_MODIFY | IN_CREATE | IN_DELETE );
    if (inotifyWd < 0) {
        ALOGE("inotify_add_watch failed, error: '%s'", strerror(errno));
        close(inotifyFd);
        return -1;
    }
    ALOGD_IF(verbose, "inotify_add_watch: inotifyWd = %d", inotifyWd);

    FD_ZERO(&set_fds);
    FD_SET(inotifyFd, &set_fds);
    max_fd = (inotifyFd > inotifyWd) ? inotifyFd : inotifyWd;

    do {
        if (isACLPresent() && (inotifyACLWd == -1)) {
            /* ACL app directory exists, watch that too */
            inotifyACLWd = inotify_add_watch(inotifyFd, DEFAULT_WEBAPP_DIR"/"ACL_APP_ID,
                    IN_MOVED_TO | IN_MODIFY | IN_CREATE | IN_DELETE );
            if (inotifyACLWd < 0) {
                ALOGE("inotify_add_watch failed, error: '%s'", strerror(errno));
                close(inotifyFd);
                return -1;
            }
            ALOGD_IF(verbose, "Added ACL inotify watch on %s, fd %d", DEFAULT_WEBAPP_DIR"/"ACL_APP_ID, inotifyACLWd);
            max_fd = (inotifyACLWd > max_fd) ? inotifyACLWd : max_fd;
        }
        else if (!isACLPresent() && (inotifyACLWd >= 0)) {
            /* ACL app directory USED to exist but is no more.  Cancel that watch */
            ALOGD_IF(verbose, "Removing ACL inotify watch %d on %s", inotifyACLWd, DEFAULT_WEBAPP_DIR"/"ACL_APP_ID);
            inotify_rm_watch(inotifyFd, inotifyACLWd);
            inotifyACLWd = -1;
        }

        read_fds = set_fds;
        nb_ready = select (max_fd+1, &read_fds, NULL, NULL, NULL);
        ALOGD("select returned: %d", nb_ready);
        if (nb_ready < 0) {
            if (errno != EINTR) {
                ALOGE("select() failed: %s", strerror(errno));
                ret = -1;
            }
        }
        else if (nb_ready == 0) {
            /* Should not get here! */
            ALOGW_IF (verbose, "select() timed out!\n");
        }
        else {
            if (FD_ISSET (inotifyFd, &read_fds))
                ret = handleInotifyEvents(inotifyFd, inotifyWd, inotifyACLWd);
        }

    } while (true);

    /* Close sockets. */
    inotify_rm_watch(inotifyFd, inotifyWd);
    close(inotifyWd);
    close(inotifyFd);

    return ret;
}

int main(int argc, char *argv[]) {
    ALOGD ("INSTALLER version %s running...\n", GSL_VERSION);
    update_properties();

    bool present = isACLPresent();      /* Is the install package present */
    bool running;                       /* Are the appopriate services running */
    int timeout=10;
    char installStatus[PROPERTY_VALUE_MAX] = {'\0'};
    int installStatusExists;

    // Wait up to 10 seconds for surfaceflinger and zygote to come up,
    // if they're going to
    ALOGD_IF(verbose, "Waiting for services to start up...\n");
    while (!(running = isACLRunning()) && (timeout-- > 0)) {
        sleep(1);
    }
    if(timeout) {
        ALOGD_IF(verbose, "Services are running, timeout = %d\n", timeout);
    }
    else {
        ALOGD_IF(verbose, "Timed out\n");
    }

    // set install status so that the ACL app can see it as well
    installStatusExists = property_get(PROP_INSTALL_STATUS, installStatus, NULL);
    if (installStatusExists) {
        setInstallStatus(installStatus);
    }

    ALOGD_IF(verbose, "INSTALLER: ACL is %spresent and %srunning\n",
                present ? "" : "NOT ",
                running ? "" : "NOT ");

    if(MU_init(nssdb_dir) < 0) {
        ALOGE ("ERROR: failed to initialize NSS3, exiting...\n");
        return EXIT_FAILURE;
    }

    if (present && !running) {
        ALOGD_IF(verbose, "ACL present but not running\n");

        // Check if we need to update (i.e. new version pushed)
        if (needToUpdate()) {
            doUpdate(true);
        }
        else if(installStatusExists) {
            ALOGD_IF(verbose, "Previous install status: %s\n", installStatus);
            if(strcmp(installStatus, INSTALL_STATUS_FAILED) == 0) {
                // Installation failed.  Try again
                if (0 != doInstall()) {
                    setInstallStatus(INSTALL_STATUS_RETRYFAIL);
                    ALOGD("Install retry failed\n");
                    reportStatus(ERR_INSTALL_RETRY_FAIL);
                }
                else {
                    ALOGD("Install retry succeeded\n");
                }
            }
            else if((strcmp(installStatus, INSTALL_STATUS_INSTALLING) == 0) ||
                    (strcmp(installStatus, INSTALL_STATUS_UPGRADING) == 0) ||
                    (strcmp(installStatus, INSTALL_STATUS_UNINSTALLED) == 0)) {

                // Installation was interrupted or not started.
                // Do an uninstall then a reinstall.  Don't check
                // status on the uninstall; whether or not it succeeds
                // we want to do the install anyway.
                doUninstall();
                if (0 != doInstall()) {
                    ALOGD("Install recovery failed\n");
                }
                else {
                    ALOGD("Install recovery succeeded\n");
                }
            }
            else if(strcmp(installStatus, INSTALL_STATUS_UNINSTALLING) == 0) {
                if (0 != doUninstall()) {
                    ALOGD("Uninstall recovery failed\n");
                }
                else {
                    ALOGD("Uninstall recovery succeeded\n");
                }
            }
            else if(strcmp(installStatus, INSTALL_STATUS_RETRYFAIL) == 0) {
                ALOGD("Multiple installation failures\n");
                reportStatus(ERR_MULTIPLE_INSTALL_FAIL);
            }
            else if(strcmp(installStatus, INSTALL_STATUS_INSTALLED) != 0) {
                // The install status is something we're not expecting
                // Do an install.  The installer script can parse the
                // install status and decide what to do.
                ALOGD("Unknown install status %s. Performing installation\n", installStatus);
                doInstall();
            }
        }
        else {
            // If the install property is not set at all then just do the install
            doInstall();
        }
    } else if (present && running) {
        ALOGD("ACL is present and running");
        // check version
        if (needToUpdate())
            doUpdate(true);
    } else {
        // If it's not present and there's an uninstaller script available then
        // we should uninstall regardless of whether the service is running.
        // The uninstall script can handle the task of stopping it.
        if (0 == access(INSTALLER_SCRIPT_DIRECTORY"/"INSTALLER_SCRIPT, 0)) {
            ALOGD_IF(verbose, "ACL is not present and not running; doing uninstall");
            doUninstall();
        }
    }

    // TODO: check if previous actions failed
    if (run() < 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

