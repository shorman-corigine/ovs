/*
 * Copyright (c) 2016 VMware, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include "Conntrack.h"
#include "PacketParser.h"
#include "util.h"

/* Eg: 227 Entering Passive Mode (a1,a2,a3,a4,p1,p2)*/
#define FTP_PASV_RSP_PREFIX "227"
#define FTP_EXTEND_PASV_RSP_PREFIX "229"
#define FTP_EXTEND_ACTIVE_RSP_PREFIX "200"

typedef enum FTP_TYPE {
    FTP_TYPE_PASV = 1,
    FTP_TYPE_ACTIVE,
    FTP_EXTEND_TYPE_PASV,
    FTP_EXTEND_TYPE_ACTIVE
} FTP_TYPE;

static __inline UINT32
OvsStrncmp(const char *s1, const char *s2, size_t n)
{
    if (!s1 || !s2) {
        return 0;
    }

    const char *s2end = s2 + n;
    while (s2 < s2end && *s2 != '\0' && toupper(*s1) == toupper(*s2)) {
        s1++, s2++;
    }

    if (s2end == s2) {
        return 0;
    }

    return (UINT32)(toupper(*s1) - toupper(*s2));
}

static __inline VOID
OvsStrlcpy(char *dest, const char *src, size_t size)
{
    /* XXX Replace ret with strlen(src) instead. */
    size_t ret = size;
    if (size) {
       size_t len = (ret >= size) ? size - 1 : ret;
       memcpy(dest, src, len);
       dest[len] = '\0';
   }
}

/*
 *---------------------------------------------------------------------------
 * OvsCtExtractNumbers
 * Returns an array of numbers after parsing the string.
 *    Eg: PASV: 192,168,0,1,5,6 -> {192,168,0,1,5,6}
 *        EPRT: 192.168.0.1 -> {192,168,0,1}
 *
 *---------------------------------------------------------------------------
 */
static __inline NDIS_STATUS
OvsCtExtractNumbers(char *buf,
                    UINT32 bufLen,
                    UINT32 arr[],
                    UINT32 arrLen,
                    char delimiter)
{
    if (!buf) {
        return  NDIS_STATUS_INVALID_PACKET;
    }

    UINT32 i = 0;

    while (*buf != '\0') {
        if (i >= bufLen || i >= arrLen) {
            /* Non-standard FTP command */
            return NDIS_STATUS_INVALID_PARAMETER;
        }

        /* Parse the number */
        if (*buf >= '0' && *buf <= '9') {
            arr[i] = arr[i] * 10 + *buf - '0';
        } else if (*buf == delimiter) {
            i++;
        } else {
            /* End of FTP response is either ) or \r\n */
            if (*buf == ')' || *buf == '\r' || *buf == '\n') {
                return NDIS_STATUS_SUCCESS;
            }
            /* Could be non-numerals or space */
        }
        buf++;
    }

    /* Parsing ended without the correct format */
    return NDIS_STATUS_INVALID_PARAMETER;
}

/*
 *----------------------------------------------------------------------------
 * OvsCtHandleFtp
 *     Extract the FTP control data from the packet and created a related
 *     entry if it's a valid connection. This method doesn't support extended
 *     FTP yet. Supports PORT and PASV commands.
 *     Eg:
 *     'PORT 192,168,137,103,192,22\r\n' -> '192.168.137.103' and 49174
 *     '227 Entering Passive Mode (192,168,137,104,194,14)\r\n' gets extracted
 *      to '192.168.137.104' and 49678
 *----------------------------------------------------------------------------
 */
NDIS_STATUS
OvsCtHandleFtp(PNET_BUFFER_LIST curNbl, OvsFlowKey *key,
               OVS_PACKET_HDR_INFO *layers, UINT64 currentTime,
               POVS_CT_ENTRY entry)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    FTP_TYPE ftpType = 0;
    const char *buf;
    char temp[256] = { 0 };
    char ftpMsg[256] = { 0 };
    UINT32 len;
    TCPHdr tcpStorage;
    const TCPHdr *tcp;
    tcp = OvsGetTcpHeader(curNbl, layers, &tcpStorage, &len);
    if (!tcp) {
        return NDIS_STATUS_INVALID_PACKET;
    }

    if (len > sizeof(temp)) {
        /* We only care up to 256 */
        len = sizeof(temp);
    }

    buf = OvsGetPacketBytes(curNbl, len,
                            layers->l4Offset + TCP_HDR_LEN(tcp),
                            temp);
    if (buf == NULL) {
        return NDIS_STATUS_INVALID_PACKET;
    }

    OvsStrlcpy((char *)ftpMsg, (char *)buf, min(len, sizeof(ftpMsg)));
    char *req = NULL;

    if ((len >= 5) && (OvsStrncmp("PORT", ftpMsg, 4) == 0)) {
        ftpType = FTP_TYPE_ACTIVE;
        req = ftpMsg + 4;
    } else if ((len >= 5) && (OvsStrncmp("EPRT", ftpMsg, 4) == 0)) {
        ftpType = FTP_EXTEND_TYPE_ACTIVE;
        req = ftpMsg + 4;
    }

    if ((len >= 4) && (OvsStrncmp(FTP_PASV_RSP_PREFIX, ftpMsg, 3) == 0)) {
        ftpType = FTP_TYPE_PASV;
        /* There are various formats for PASV command. We try to support
         * some of them. This has been addressed by RFC 2428 - EPSV.
         * Eg:
         *    227 Entering Passive Mode (h1,h2,h3,h4,p1,p2).
         *    227 Entering Passive Mode (h1,h2,h3,h4,p1,p2
         *    227 Entering Passive Mode. h1,h2,h3,h4,p1,p2
         *    227 =h1,h2,h3,h4,p1,p2
         */
        char *paren;
        paren = strchr(ftpMsg, '(');
        if (paren) {
            req = paren + 1;
        } else {
            /* PASV command without ( */
            req = ftpMsg + 3;
        }
    } else if ((len >= 4) && (
               OvsStrncmp(FTP_EXTEND_PASV_RSP_PREFIX, ftpMsg, 3) == 0)) {
        ftpType = FTP_EXTEND_TYPE_PASV;
        /* The ftp extended passive mode only contain port info, ip address
         * is same with the network protocol used by control connection.
         * 229 Entering Extended Passive Mode (|||port|)
         * */
        char *paren;
        paren = strchr(ftpMsg, '|');
        if (paren) {
            req = paren + 3;
        } else {
            /* Not a valid EPSV packet. */
            return NDIS_STATUS_INVALID_PACKET;
        }

        if (!(*req > '0' && * req < '9')) {
            /* Not a valid port number. */
            return NDIS_STATUS_INVALID_PACKET;
        }
    }

    if (req == NULL) {
        /* Not a PORT/PASV control packet */
        return NDIS_STATUS_SUCCESS;
    }

    struct ct_addr clientIp = {0}, serverIp = {0};
    UINT16 port = 0;

    if (ftpType == FTP_TYPE_ACTIVE || ftpType == FTP_TYPE_PASV) {
        UINT32 arr[6] = {0};
        status = OvsCtExtractNumbers(req, len, arr, 6, ',');

        if (status != NDIS_STATUS_SUCCESS) {
            return status;
        }

        UINT32 ip = ntohl((arr[0] << 24) | (arr[1] << 16) |
                          (arr[2] << 8) | arr[3]);
        port = ntohs(((arr[4] << 8) | arr[5]));

        if (ftpType == FTP_TYPE_ACTIVE) {
            serverIp.ipv4 = key->ipKey.nwDst;
            clientIp.ipv4 = ip;
        }

        if (ftpType == FTP_TYPE_PASV) {
            serverIp.ipv4 = ip;
            clientIp.ipv4 = key->ipKey.nwDst;
        }
    } else {
        if (ftpType == FTP_EXTEND_TYPE_ACTIVE) {
            /** In ftp active mode, we need to parse string like below:
             * " |2|20::1|50778|", 2 represent address is ipv6, 1 represent
             * address family is ipv4, "20::1" is ipv6 address, 50779 is port
             * client need to listen.
             * **/
            char *curHdr = NULL;
            char *nextHdr = NULL;
            int index = 0;
            int isIpv6AddressFamily = 0;
            char ftpStr[512] = {0x00};

            RtlCopyMemory(ftpStr, req, strlen(req));
            for (curHdr = ftpStr; *curHdr != '|'; curHdr++);
            curHdr = curHdr + 1;;
            do {
                /** index == 0 parse address family,
                 *  index == 1 parse address,
                 *  index == 2 parse port **/
                for (nextHdr = curHdr; *nextHdr != '|'; nextHdr++);
                *nextHdr = '\0';

                if (*curHdr == '0' || !curHdr || index > 2) {
                    break;
                }

                if (index == 0 && *curHdr == '1') {
                    isIpv6AddressFamily = 0;
                } else if (index == 0 && *curHdr == '2') {
                    isIpv6AddressFamily = 1;
                }

                if (index == 1 && isIpv6AddressFamily) {
                    OvsIpv6StringToAddress(curHdr, &clientIp.ipv6);
                }

                if (index == 2) {
                    for (char *tmp = curHdr; *tmp != '\0'; tmp++) {
                        port = port * 10 + (*tmp - '0');
                    }
                    port = htons(port);
                }

                curHdr = nextHdr + 1;
                index++;
            } while (1);

            if (index < 2) { /* Not valid packet due to less than three parameter */
                return NDIS_STATUS_SUCCESS;
            }
            serverIp.ipv6 = key->ipv6Key.ipv6Dst;
        }

        if (ftpType == FTP_EXTEND_TYPE_PASV) {
            /* Here used to parse the string "229 Entering Extended Passive Mode (|||50522|),
             * 50522 is the port we want". */
            char *tmp = req;
            while (*tmp != '|' && *tmp != '\0') {
                port = port * 10 + (*tmp - '0');
                tmp++;
            }

            port = htons(port);

            serverIp.ipv6 = key->ipv6Key.ipv6Src;
            clientIp.ipv6 = key->ipv6Key.ipv6Dst;
        }
    }

    switch (ftpType) {
    case FTP_TYPE_PASV:
        /* Ensure that the command states Server's IP address */
        OvsCtRelatedEntryCreate(key->ipKey.nwProto,
                                key->l2.dlType,
                                /* Server's IP */
                                serverIp,
                                /* Use intended client's IP */
                                clientIp,
                                /* Dynamic port opened on server */
                                port,
                                /* We don't know the client port */
                                0,
                                currentTime,
                                entry);
        break;
    case FTP_TYPE_ACTIVE:
        OvsCtRelatedEntryCreate(key->ipKey.nwProto,
                                key->l2.dlType,
                                /* Server's default IP address */
                                serverIp,
                                /* Client's IP address */
                                clientIp,
                                /* FTP Data Port is 20 */
                                ntohs(IPPORT_FTP_DATA),
                                /* Port opened up on Client */
                                port,
                                currentTime,
                                entry);
        break;
    case FTP_EXTEND_TYPE_PASV:
        OvsCtRelatedEntryCreate(key->ipv6Key.nwProto,
                                key->l2.dlType,
                                serverIp,
                                clientIp,
                                port,
                                0,
                                currentTime,
                                entry);
        break;
    case FTP_EXTEND_TYPE_ACTIVE:
        OvsCtRelatedEntryCreate(key->ipv6Key.nwProto,
                                key->l2.dlType,
                                /* Server's default IP address */
                                serverIp,
                                /* Client's IP address */
                                clientIp,
                                /* FTP Data Port is 20 */
                                ntohs(IPPORT_FTP_DATA),
                                /* Port opened up on Client */
                                port,
                                currentTime,
                                entry);
        break;
    default:
        OVS_LOG_ERROR("invalid ftp type:%d", ftpType);
        status = NDIS_STATUS_INVALID_PARAMETER;
        break;
    }

    return status;
}
