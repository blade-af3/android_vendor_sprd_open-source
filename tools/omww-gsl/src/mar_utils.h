/*********************************************************************************
 *
 * Copyright (c) 2014 OpenMobile Worldwide Inc.  All rights reserved.
 * Confidential and Proprietary to OpenMobile Worldwide, Inc.
 * This software is provided under nondisclosure agreement for evaluation purposes only.
 * Not for distribution.
 *
 *********************************************************************************/

#ifndef MAR_UTILS_H_
#define MAR_UTILS_H_


extern int MU_init (const char * nss_db_path);

extern int MU_verifyMarfile (const char * marfile, const char * public_key_directory, int verbose);

extern int MU_extractMarfile (const char * marfile, const char * extract_dir);


#endif /* MAR_UTILS_H_ */
