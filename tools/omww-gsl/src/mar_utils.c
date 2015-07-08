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
#include <utils/Log.h>
#include <dirent.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "mar.h"
#include "mar_cmdline.h"
#include "nss.h"
#include "secerr.h"

#include "mar_utils.h"

#define MU_BASE_NSS_DB_DIR  "/data/b2g/mozilla"
#define MU_CERT_DB_FILENAME "cert9.db"
#define MU_KEY_DB_FILENAME  "key4.db"
#define MU_PKCS11_FILENAME  "pkcs11.txt"


static int nssdb_scandir_filter (const struct dirent * key_file) {
    size_t len;
    struct stat file_data;
    char path[MAXPATHLEN];

    if (!key_file) goto rejected;

    /* Look for a sub-directory... */
    if (key_file->d_type != DT_DIR) goto rejected;

    /* ..that contains a MU_CERT_DB_FILENAME file... */
    if (snprintf (path, sizeof(path),
                  MU_BASE_NSS_DB_DIR "/%s/" MU_CERT_DB_FILENAME,
                  key_file->d_name) >= (int) sizeof(path)) goto rejected;
    if (stat (path, &file_data) == -1) goto rejected;

    /* ..and a MU_KEY_DB_FILENAME file... */
    if (snprintf (path, sizeof(path),
                  MU_BASE_NSS_DB_DIR "/%s/" MU_KEY_DB_FILENAME,
                  key_file->d_name) >= (int) sizeof(path)) goto rejected;
    if (stat (path, &file_data) == -1) goto rejected;

    /* ..and a MU_PKCS11_FILENAME file. */
    if (snprintf (path, sizeof(path),
                  MU_BASE_NSS_DB_DIR "/%s/" MU_PKCS11_FILENAME,
                  key_file->d_name) >= (int) sizeof(path)) goto rejected;
    if (stat (path, &file_data) == -1) goto rejected;

    return 1;

rejected:
    return 0;
}

static int key_scandir_filter (const struct dirent * key_file) {
    size_t len;

    /* Look for all regular files... */
    if (!key_file || key_file->d_type != DT_REG) goto rejected;

    /* that end in ".der". */
    len = strnlen (key_file->d_name, sizeof(key_file->d_name));
    if (len < 4) goto rejected;

    if (strncasecmp (&(key_file->d_name[len-4]), ".der", 4)) goto rejected;

    return 1;

rejected:
    return 0;
}


int MU_init (const char * nss_db_path) {
    int result = -1;
    int dir_count = 0;
    struct dirent **db_dir_list = NULL;
    char path_buff[MAXPATHLEN];
    int i;

    /* If database path has not been given, then attempt
       to look for the database directory on the device. */
    if (!nss_db_path) {
        dir_count = scandir (MU_BASE_NSS_DB_DIR, &db_dir_list, nssdb_scandir_filter, NULL);
        if (dir_count <=0 || !db_dir_list || !(db_dir_list[0])) {
            ALOGE ("Failure finding NSS database directory! Try passing it in the command line.\n");
            goto failure;
        }

        if (snprintf (path_buff, sizeof(path_buff),
                      MU_BASE_NSS_DB_DIR "/%s",
                      db_dir_list[0]->d_name) >= (int) sizeof(path_buff)) {
            ALOGE ("Failure finding NSS database directory! Try passing it in the command line.\n");
            goto failure;
        }

        nss_db_path = path_buff;
    }

    /* Initialize NSS */
    if (NSS_Initialize (nss_db_path, "", "", SECMOD_DB, NSS_INIT_READONLY) != SECSuccess) {
        int error = PORT_GetError ();
        ALOGE ("Failure initializing NSS! error = %d\n", error-SEC_ERROR_BASE);
        goto failure;
    }

    result = 0;

failure:
    /* Cleanup. */
    if (db_dir_list) {
        for (i = 0; i < dir_count; i++) {
            if (db_dir_list[i]) free (db_dir_list[i]);
        }
        free (db_dir_list);
    }

    return result;
}


int MU_verifyMarfile (const char * marfile, const char * public_key_directory, int verbose) {
    int result = -1;
    int key_count = 0;
    int i;
    int verified = 0;
    struct dirent **key_file_list = NULL;
    char ** key_filename_array = NULL;
    size_t dir_path_len = 0;
    size_t path_len;

    if (!marfile || !public_key_directory) goto failure;

    /* Loop through all keys in the key directory until one successfully verifies the signature. */
    key_count = scandir (public_key_directory, &key_file_list, key_scandir_filter, NULL);
    if (key_count <= 0 || !key_file_list) {
        ALOGE_IF (verbose, "No keys could be found in \"%s\"!\n", public_key_directory);
        goto failure;
    }

    /* Allocate filename array. */
    key_filename_array = (char**) calloc(sizeof(char*), key_count);
    if (!key_filename_array) {
        ALOGE_IF (verbose, "Out of memory!\n");
        goto failure;
    }

    /* Copy filenames into the array. */
    dir_path_len = strnlen (public_key_directory, MAXPATHLEN);
    for (i = 0; i < key_count; i++) {
        if (!key_file_list[i]) goto failure;
        path_len = dir_path_len + strnlen (key_file_list[i]->d_name, MAXPATHLEN) + 2;
        key_filename_array[i] = malloc (path_len);
        if (!key_filename_array[i]) goto failure;
        if (snprintf (key_filename_array[i], path_len,
                      "%s/%s", public_key_directory,
                      key_file_list[i]->d_name) >= (int)path_len) goto failure;
    }

    /* Attempt to verify the MAR file with all found signatures. */
    verified = 0;
    for (i = 0; i < key_count; i++) {
        if (!mar_verify_signatures_from_files (marfile,
                    (const char * const*) &key_filename_array[i], 1)) {
            ALOGD_IF (verbose, "Verified with key file: \"%s\".\n", key_filename_array[i]);
            verified = 1;
            break;
        }

        ALOGD_IF (verbose, "Could not verify with key file: \"%s\"!\n", key_filename_array[i]);
    }

    if (!verified) {
        ALOGE_IF (verbose, "Could not verify with all keys in key directory!\n");
        goto failure;
    }

    result = 0;

failure:

    /* Cleanup */
    if (key_file_list) {
        for (i = 0; i < key_count; i++) {
            if (key_file_list[i]) free (key_file_list[i]);
        }
        free (key_file_list);
    }

    if (key_filename_array) {
        for (i = 0; i < key_count; i++) {
            if (key_filename_array[i]) free (key_filename_array[i]);
        }
        free (key_filename_array);
    }

    return result;
}


int MU_extractMarfile (const char * marfile, const char * extract_dir) {
    int result = -1;
    char dir[MAXPATHLEN];

    if (getcwd (dir, sizeof(dir))) {
        if (chdir (extract_dir) == 0) {
            if (mar_extract (marfile) == 0) {
                result = 0;
            }
            chdir (dir);
        }
    }
    return result;
}

