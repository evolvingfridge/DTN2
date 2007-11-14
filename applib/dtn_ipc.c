/*
 *    Copyright 2004-2006 Intel Corporation
 * 
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 * 
 *        http://www.apache.org/licenses/LICENSE-2.0
 * 
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#  include <dtn-config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <oasys/compat/inet_aton.h>
#include <oasys/compat/inttypes.h>

#include "dtn_ipc.h"
#include "dtn_errno.h"
#include "dtn_types.h"

/* exposed globally for testing purposes only */
int dtnipc_version = DTN_IPC_VERSION;

const char*
dtnipc_msgtoa(u_int8_t type)
{
#define CASE(_type) case _type : return #_type; break;
    
    switch(type) {
        CASE(DTN_OPEN);
        CASE(DTN_CLOSE);
        CASE(DTN_LOCAL_EID);
        CASE(DTN_REGISTER);
        CASE(DTN_UNREGISTER);
        CASE(DTN_FIND_REGISTRATION);
        CASE(DTN_CHANGE_REGISTRATION);
        CASE(DTN_BIND);
        CASE(DTN_SEND);
        CASE(DTN_RECV);
        CASE(DTN_BEGIN_POLL);
        CASE(DTN_CANCEL_POLL);
        CASE(DTN_CANCEL);

    default:
        return "(unknown type)";
    }
    
#undef CASE
}

/*
 * Initialize the handle structure.
 */
int
dtnipc_open(dtnipc_handle_t* handle)
{
    int remote_version, ret;
    char *env, *end;
    struct sockaddr_in sa;
    in_addr_t ipc_addr;
    u_int16_t ipc_port;
    u_int32_t handshake;
    u_int port;

    // zero out the handle
    memset(handle, 0, sizeof(dtnipc_handle_t));
    
    // note that we leave eight bytes free to be used for the framing
    // -- the type code and length for send (which is only five
    // bytes), and the return code and length for recv (which is
    // actually eight bytes)
    xdrmem_create(&handle->xdr_encode, handle->buf + 8,
                  DTN_MAX_API_MSG - 8, XDR_ENCODE);
    
    xdrmem_create(&handle->xdr_decode, handle->buf + 8,
                  DTN_MAX_API_MSG - 8, XDR_DECODE);

    // open the socket
    handle->sock = socket(PF_INET, SOCK_STREAM, 0);
    if (handle->sock < 0)
    {
        handle->err = DTN_ECOMM;
        dtnipc_close(handle);
        return -1;
    }

    // check for DTNAPI environment variables overriding the address /
    // port defaults
    ipc_addr = htonl(INADDR_LOOPBACK);
    ipc_port = DTN_IPC_PORT;
    
    if ((env = getenv("DTNAPI_ADDR")) != NULL) {
        if (inet_aton(env, (struct in_addr*)&ipc_addr) == 0)
        {
            fprintf(stderr, "DTNAPI_ADDR environment variable (%s) "
                    "not a valid ip address\n", env);
            exit(1);
        }
    }

    if ((env = getenv("DTNAPI_PORT")) != NULL) {
        port = strtoul(env, &end, 10);
        if (*end != '\0' || port > 0xffff)
        {
            fprintf(stderr, "DTNAPI_PORT environment variable (%s) "
                    "not a valid ip port\n", env);
            exit(1);
        }
        ipc_port = (u_int16_t)port;
    }

    // connect to the server
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = ipc_addr;
    sa.sin_port = htons(ipc_port);
    
    ret = connect(handle->sock, (const struct sockaddr*)&sa, sizeof(sa));
    if (ret != 0) {
        handle->err = DTN_ECOMM;
        dtnipc_close(handle);
        return -1;
    }

    // send the session initiation to the server on the handshake
    // port. it consists of DTN_OPEN in the high 16 bits and IPC
    // version in the low 16 bits
    handshake = htonl(DTN_OPEN << 16 | dtnipc_version);
    ret = write(handle->sock, &handshake, sizeof(handshake));
    if (ret != sizeof(handshake)) {
        handle->err = DTN_ECOMM;
        dtnipc_close(handle);
        return -1;
    }

    // wait for the handshake response
    handshake = 0;
    ret = read(handle->sock, &handshake, sizeof(handshake));
    if (ret != sizeof(handshake) || (ntohl(handshake) >> 16) != DTN_OPEN) {
        dtnipc_close(handle);
        handle->err = DTN_ECOMM;
        return -1;
    }

    remote_version = (ntohl(handshake) & 0x0ffff);
    if (remote_version != dtnipc_version) {
        dtnipc_close(handle);
        handle->err = DTN_EVERSION;
        return -1;
    }
    
    return 0;
}

/*
 * Clean up the handle. dtnipc_open must have already been called on
 * the handle.
 */
int
dtnipc_close(dtnipc_handle_t* handle)
{
    int ret;
    
    // first send a close over RPC
    if (handle->err != DTN_ECOMM) {
        ret = dtnipc_send_recv(handle, DTN_CLOSE);
    } else {
        ret = -1;
    }
    
    xdr_destroy(&handle->xdr_encode);
    xdr_destroy(&handle->xdr_decode);

    if (handle->sock > 0) {
        close(handle->sock);
    }

    handle->sock = 0;

    return ret;
}
      

/*
 * Send a message over the dtn ipc protocol.
 *
 * Returns 0 on success, -1 on error.
 */
int
dtnipc_send(dtnipc_handle_t* handle, dtnapi_message_type_t type)
{
    int ret;
    u_int32_t len, msglen;
    
    // pack the message code in the fourth byte of the buffer and the
    // message length into the next four. we don't use xdr routines
    // for these since we need to be able to decode the length on the
    // other side to make sure we've read the whole message, and we
    // need the type to know which xdr decoder to call
    handle->buf[3] = type;

    len = xdr_getpos(&handle->xdr_encode);
    msglen = len + 5;
    len = htonl(len);
    memcpy(&handle->buf[4], &len, sizeof(len));
    
    // reset the xdr encoder
    xdr_setpos(&handle->xdr_encode, 0);
    
    // send the message, looping until it's all sent
    char* bp = &handle->buf[3];
    do {
        ret = write(handle->sock, bp, msglen);
        
        if (ret <= 0) {
            if (errno == EINTR)
                continue;
            
            handle->err = DTN_ECOMM;
            dtnipc_close(handle);
            return -1;
        }

        bp     += ret;
        msglen -= ret;
        
    } while (msglen > 0);
    
    return 0;
}

/*
 * Receive a message on the ipc channel. May block if there is no
 * pending message.
 *
 * Sets status to the server-returned status code and returns the
 * length of any reply message on success, returns -1 on internal
 * error.
 */
int
dtnipc_recv(dtnipc_handle_t* handle, int* status)
{
    int ret;
    u_int32_t len, nread;
    u_int32_t statuscode;

    // reset the xdr decoder before reading in any data
    xdr_setpos(&handle->xdr_decode, 0);

    // read the status code and length
    ret = read(handle->sock, handle->buf, 8);

    // make sure we got it all
    if (ret != 8) {
        handle->err = DTN_ECOMM;
        dtnipc_close(handle);
        return -1;
    }
    
    memcpy(&statuscode, handle->buf, sizeof(statuscode));
    statuscode = ntohl(statuscode);
    *status = statuscode;
    
    memcpy(&len, &handle->buf[4], sizeof(len));
    len = ntohl(len);

    // read the rest of the message 
    nread = 8;
    while (nread < len + 8) {
        ret = read(handle->sock,
                   &handle->buf[nread], sizeof(handle->buf) - nread);
        if (ret <= 0) {
            if (errno == EINTR)
                continue;
            
            handle->err = DTN_ECOMM;
            dtnipc_close(handle);
            return -1;
        }

        nread += ret;
    }

    return len;
}


/**
 * Send a message and wait for a response over the dtn ipc protocol.
 *
 * Returns 0 on success, -1 on error.
 */
int dtnipc_send_recv(dtnipc_handle_t* handle, dtnapi_message_type_t type)
{
    int status;

    // send the message
    if (dtnipc_send(handle, type) < 0) {
        return -1;
    }

    // wait for a response
    if (dtnipc_recv(handle, &status) < 0) {
        return -1;
    }

    // handle server-side errors
    if (status != DTN_SUCCESS) {
        handle->err = status;
        return -1;
    }

    return 0;
}
