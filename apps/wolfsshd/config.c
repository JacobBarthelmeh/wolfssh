/* config.c
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

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#ifdef WOLFSSH_SSHD
/* functions for parsing out options from a config file and for handling loading
 * key/certs using the env. filesystem */

#include <wolfssh/ssh.h>
#include <wolfssh/internal.h>
#include <wolfssh/log.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#ifdef NO_INLINE
    #include <wolfssh/misc.h>
#else
    #define WOLFSSH_MISC_INCLUDED
    #include "src/misc.c"
#endif

#include "wolfsshd.h"

struct WOLFSSHD_CONFIG {
    void* heap;
    char* banner;
    char* chrootDir;
    char* ciphers;
    char* hostKey;
    char* hostKeyAlgos;
    char* kekAlgos;
    char* listenAddress;
    char* authKeysFile;
    long  loginTimer;
    word16 port;
    byte usePrivilegeSeparation;
    byte passwordAuth:1;
    byte pubKeyAuth:1;
    byte permitRootLogin:1;
    byte permitEmptyPasswords:1;
};


/* convert a string into seconds, handles if 'm' for minutes follows the string
 * number, i.e. 2m
 * Returns the value on success and negative value on failure */
static long wolfSSHD_GetConfigInt(const char* in, int inSz, int isTime,
    void* heap)
{
    long ret = 0;
    int mult = 1; /* multiplier */
    int idx  = 0;
    int sz   = 0;

    /* remove leading white spaces */
    while (idx < inSz && in[idx] == ' ') idx++;

    if (idx == inSz) {
        ret = WS_BAD_ARGUMENT;
    }

    /* remove trailing white spaces */
    if (ret == 0) {
        for (sz = 1; sz + idx < inSz; sz++) {
            if (in[sz + idx] == ' ') break;
            if (in[sz + idx] == '\n') break;
        }
    }

    /* check for multipliers */
    if (isTime && ret == 0) {
        if (in[sz - 1 + idx] == 'm') {
            sz--;
            mult = 60;
        }
        if (in[sz - 1 + idx] == 'h') {
            sz--;
            mult = 60*60;
        }
    }

    if (ret == 0) {
        char* num = (char*)WMALLOC(sz + 1, heap, DYNTYPE_SSHD);
        if (num == NULL) {
            ret = WS_MEMORY_E;
        }
        else {
            WMEMCPY(num, in + idx, sz);
            num[sz] = '\0';
            ret = atol(num);
            if (ret > 0) {
                ret = ret * mult;
            }
            WFREE(num, heap, DYNTYPE_SSHD);
        }
    }

    return ret;
}

/* returns WS_SUCCESS on success */
static int wolfSSHD_CreateString(char** out, const char* in, int inSz,
        void* heap)
{
    int ret = WS_SUCCESS;
    int idx = 0;

    /* remove leading white spaces */
    while (idx < inSz && in[idx] == ' ') idx++;

    if (idx == inSz) {
        ret = WS_BAD_ARGUMENT;
    }

    /* malloc new string and set it */
    if (ret == WS_SUCCESS) {
        *out = (char*)WMALLOC((inSz - idx) + 1, heap, DYNTYPE_SSHD);
        if (*out == NULL) {
            ret = WS_MEMORY_E;
        }
        else {
            XMEMCPY(*out, in + idx, inSz - idx);
            *(*out + (inSz - idx)) = '\0';
        }
    }

    return ret;
}

static void wolfSSHD_FreeString(char** in, void* heap)
{
    if (*in != NULL) {
        WFREE(*in, heap, DYNTYPE_SSHD);
        *in = NULL;
    }
    (void)heap;
}

WOLFSSHD_CONFIG* wolfSSHD_NewConfig(void* heap)
{
    WOLFSSHD_CONFIG* ret;

    ret = (WOLFSSHD_CONFIG*)WMALLOC(sizeof(WOLFSSHD_CONFIG), heap,
                DYNTYPE_SSHD);
    if (ret == NULL) {
        printf("issue mallocing config structure for sshd\n");
    }
    else {
        WMEMSET(ret, 0, sizeof(WOLFSSHD_CONFIG));

        /* default values */
        ret->port = 9387;
    }
    return ret;

}

void wolfSSHD_FreeConfig(WOLFSSHD_CONFIG* conf)
{
    void* heap;

    if (conf != NULL) {
        heap = conf->heap;

        wolfSSHD_FreeString(&conf->authKeysFile, heap);

        WFREE(conf, heap, DYNTYPE_SSHD);
    }
}

#define MAX_LINE_SIZE 160

/* returns WS_SUCCESS on success
 * Fails if any option is found that is unknown/unsupported
 */
static int wolfSSHD_ParseConfigLine(WOLFSSHD_CONFIG* conf, const char* l,
        int lSz)
{
    int ret = WS_BAD_ARGUMENT;
    int sz;
    char* tmp;

    /* supported config options */
    const char authKeyFile[]          = "AuthorizedKeysFile";
    const char privilegeSeparation[]  = "UsePrivilegeSeparation";
    const char loginGraceTime[]       = "LoginGraceTime";
    const char permitEmptyPasswords[] = "PermitEmptyPasswords";

    sz = (int)XSTRLEN(authKeyFile);
    if (lSz > sz && XSTRNCMP(l, authKeyFile, sz) == 0) {
        ret = wolfSSHD_CreateString(&tmp, l + sz, lSz - sz, conf->heap);
        if (ret == WS_SUCCESS) {
            wolfSSHD_SetAuthKeysFile(conf, tmp);
        }
    }

    sz = (int)XSTRLEN(privilegeSeparation);
    if (lSz > sz && XSTRNCMP(l, privilegeSeparation, sz) == 0) {
        char* privType = NULL;
        ret = wolfSSHD_CreateString(&privType, l + sz, lSz - sz, conf->heap);

        /* check if is an allowed option */
        if (XSTRNCMP(privType, "sandbox", 7) == 0) {
            wolfSSH_Log(WS_LOG_INFO, "[SSHD] Sandbox privilege separation");
            ret = WS_SUCCESS;
        }

        if (XSTRNCMP(privType, "yes", 3) == 0) {
            wolfSSH_Log(WS_LOG_INFO, "[SSHD] Privilege separation enabled");
            ret = WS_SUCCESS;
        }

        if (XSTRNCMP(privType, "no", 2) == 0) {
            wolfSSH_Log(WS_LOG_INFO, "[SSHD] Turning off privilege separation!");
            ret = WS_SUCCESS;
        }

        if (ret != WS_SUCCESS) {
            wolfSSH_Log(WS_LOG_ERROR,
                    "[SSHD] Unknown/supported privilege separation!");
        }
        wolfSSHD_FreeString(&privType, conf->heap);
    }

    if (XSTRNCMP(l, "Subsystem", 9) == 0) {
        //@TODO
        ret = WS_SUCCESS;
    }

    if (XSTRNCMP(l, "ChallengeResponseAuthentication", 31) == 0) {
        //@TODO
        ret = WS_SUCCESS;
    }

    if (XSTRNCMP(l, "UsePAM", 6) == 0) {
        //@TODO
        ret = WS_SUCCESS;
    }

    if (XSTRNCMP(l, "X11Forwarding", 13) == 0) {
        //@TODO
        ret = WS_SUCCESS;
    }

    if (XSTRNCMP(l, "PrintMotd", 9) == 0) {
        //@TODO
        ret = WS_SUCCESS;
    }

    if (XSTRNCMP(l, "AcceptEnv", 9) == 0) {
        //@TODO
        ret = WS_SUCCESS;
    }

    if (XSTRNCMP(l, "Protocol", 8) == 0) {
        //@TODO
        ret = WS_SUCCESS;
    }

    sz = (int)XSTRLEN(loginGraceTime);
    if (lSz > sz && XSTRNCMP(l, loginGraceTime, sz) == 0) {
        long num;

        num = wolfSSHD_GetConfigInt(l + sz, lSz - sz, 1, conf->heap);
        if (num < 0) {
            wolfSSH_Log(WS_LOG_ERROR, "[SSHD] Issue getting login grace time");
        }
        else {
            ret = WS_SUCCESS;
            conf->loginTimer = num;
            wolfSSH_Log(WS_LOG_INFO, "[SSHD] Setting login grace time to %ld",
                num);
        }
    }


    sz = (int)XSTRLEN(permitEmptyPasswords);
    if (lSz > sz && XSTRNCMP(l, permitEmptyPasswords, sz) == 0) {
        char* emptyPasswd = NULL;
        ret = wolfSSHD_CreateString(&emptyPasswd, l + sz, lSz - sz, conf->heap);

        if (XSTRNCMP(emptyPasswd, "yes", 3) == 0) {
            wolfSSH_Log(WS_LOG_INFO, "[SSHD] Empty password enabled");
            conf->permitEmptyPasswords = 1;
            ret = WS_SUCCESS;
        }

        /* default is no */
        if (XSTRNCMP(emptyPasswd, "no", 2) == 0) {
            ret = WS_SUCCESS;
        }
        wolfSSHD_FreeString(&emptyPasswd, conf->heap);
    }

    if (ret == WS_BAD_ARGUMENT) {
        printf("unknown / unsuported config line\n");
    }

    (void)lSz;
    return ret;
}


int wolfSSHD_LoadSSHD(WOLFSSHD_CONFIG* conf, const char* filename)
{
    XFILE f;
    int ret = WS_SUCCESS;
    char buf[MAX_LINE_SIZE];
    const char* current;

    if (conf == NULL || filename == NULL)
        return BAD_FUNC_ARG;

    f = XFOPEN(filename, "rb");
    if (f == XBADFILE) {
        wolfSSH_Log(WS_LOG_ERROR, "Unable to open SSHD config file %s\n",
                filename);
        return BAD_FUNC_ARG;
    }
    wolfSSH_Log(WS_LOG_INFO, "[SSHD] parsing config file %s", filename);

    while ((current = XFGETS(buf, MAX_LINE_SIZE, f)) != NULL) {
        int currentSz = (int)XSTRLEN(current);

        /* remove leading spaces */
        while (currentSz > 0 && current[0] == ' ') {
            currentSz = currentSz - 1;
            current   = current + 1;
        }

        if (currentSz <= 1) {
            continue; /* empty line */
        }

        if (current[0] == '#') {
            //printf("read commented out line\n%s\n", current);
            continue; /* commented out line */
        }

        ret = wolfSSHD_ParseConfigLine(conf, current, currentSz);
        if (ret != WS_SUCCESS) {
            printf("Unable to parse config line : %s\n", current);
            break;
        }
    }
    XFCLOSE(f);

    SetAuthKeysPattern(conf->authKeysFile);

    return ret;
}

char* wolfSSHD_GetAuthKeysFile(WOLFSSHD_CONFIG* conf)
{
    if (conf != NULL)
        return conf->authKeysFile;
    return NULL;
}

int wolfSSHD_SetAuthKeysFile(WOLFSSHD_CONFIG* conf, const char* file)
{
    if (conf == NULL) {
        return WS_BAD_ARGUMENT;
    }

    conf->authKeysFile = (char*)file;

    return WS_SUCCESS;
}

char* wolfSSHD_GetBanner(WOLFSSHD_CONFIG* conf)
{
    if (conf != NULL)
        return conf->banner;
    return NULL;
}

char* wolfSSHD_GetHostPrivateKey(WOLFSSHD_CONFIG* conf)
{
    if (conf != NULL)
        return conf->hostKey;
    return NULL;
}

int wolfSSHD_SetHostPrivateKey(WOLFSSHD_CONFIG* conf, const char* hostKeyFile)
{
    if (conf == NULL)
        return WS_BAD_ARGUMENT;

    conf->hostKey = (char*)hostKeyFile;
    return WS_SUCCESS;
}

word16 wolfSSHD_GetPort(WOLFSSHD_CONFIG* conf)
{
    if (conf != NULL)
        return conf->port;
    return 0;
}


/* test if the 'opt' options is enabled or not in 'conf' for the flags set
 * return 1 if enabled and 0 if not */
long wolfSSHD_ConfigGetOption(WOLFSSHD_CONFIG* conf, word32 opt)
{
    long ret = 0;

    switch (opt) {
        case WOLFSSHD_EMPTY_PASSWORD:
            ret = conf->permitEmptyPasswords;
            break;
        case WOLFSSHD_GRACE_LOGIN_TIME:
            ret = conf->loginTimer;
            break;
    }
    return ret;
}
#endif /* WOLFSSH_SSHD */
