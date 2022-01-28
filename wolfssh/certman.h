/* certman.h
 *
 * Copyright (C) 2014-2021 wolfSSL Inc.
 *
 * This file is part of wolfSSH.
 *
 * wolfSSH is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfSSH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wolfSSH.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * The certman module contains utility functions wrapping the wolfSSL
 * certificate manager functions to validate user certificates.
 */


#ifndef _WOLFSSH_CERTMAN_H_
#define _WOLFSSH_CERTMAN_H_

#include <wolfssh/settings.h>
#include <wolfssh/port.h>

#ifdef __cplusplus
extern "C" {
#endif


WOLFSSH_API int wolfSSH_CertInit(void);


#ifdef __cplusplus
}
#endif

#endif /* _WOLFSSH_CERTMAN_H_ */
