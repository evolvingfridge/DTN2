/*
 * NOTE: To make comments appear in the generated files, they must be
 * preceded by a % sign (unlike this one).
 */

%/*
% * IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
% * By downloading, copying, installing or using the software you agree
% * to this license. If you do not agree to this license, do not
% * download, install, copy or use the software.
% * 
% * Intel Open Source License 
% * 
% * Copyright (c) 2004 Intel Corporation. All rights reserved. 
% * 
% * Redistribution and use in source and binary forms, with or without
% * modification, are permitted provided that the following conditions
% * are met:
% * 
% *   Redistributions of source code must retain the above copyright
% *   notice, this list of conditions and the following disclaimer.
% * 
% *   Redistributions in binary form must reproduce the above copyright
% *   notice, this list of conditions and the following disclaimer in
% *   the documentation and/or other materials provided with the
% *   distribution.
% * 
% *   Neither the name of the Intel Corporation nor the names of its
% *   contributors may be used to endorse or promote products derived
% *   from * this software without specific prior written permission.
% *  
% * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
% * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
% * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
% * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
% * INTEL OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
% * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
% * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
% * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
% * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
% * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
% * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
% * OF THE POSSIBILITY OF SUCH DAMAGE.
% */
%
%/**********************************
% * This file defines the types used in the DTN client API. The structures are
% * autogenerated using rpcgen, as are the marshalling and unmarshalling
% * XDR routines.
% */
%
%#include <limits.h>
%#ifndef ARG_MAX
%#define ARG_MAX _POSIX_ARG_MAX
%#endif
%
%#include <rpc/rpc.h>
%
%
%/**********************************
% * Constants.
% * (Note that we use #defines to get the comments as well)
% */
%#define DTN_MAX_ENDPOINT_ID 256	/* max endpoint_id size (bytes) */
%#define DTN_MAX_PATH_LEN PATH_MAX	/* max path length */
%#define DTN_MAX_EXEC_LEN ARG_MAX	/* length of string passed to exec() */
%#define DTN_MAX_AUTHDATA 1024		/* length of auth/security data*/
%#define DTN_MAX_REGION_LEN 64		/* 64 chars "should" be long enough */
%#define DTN_MAX_BUNDLE_MEM 50000	/* biggest in-memory bundle is ~50K*/

%
%/**
% * Specification of a dtn endpoint id, i.e. a URI, implemented as a
% * fixed-length char buffer. Note that for efficiency reasons, this
% * fixed length is relatively small (256 bytes). 
% * 
% * The alternative is to use the string XDR type but then all endpoint
% * ids would require malloc / free which is more prone to leaks / bugs.
% */
struct dtn_endpoint_id_t {
    opaque	uri[DTN_MAX_ENDPOINT_ID];
};

%
%/**
% * A registration cookie.
% */
typedef u_int dtn_reg_id_t;

%
%/**
% * DTN timeouts are specified in seconds.
% */
typedef u_int dtn_timeval_t;

%
%/**
% * An infinite wait is a timeout of -1.
% */
%#define DTN_TIMEOUT_INF ((dtn_timeval_t)-1)

%
%/**
% * Specification of a service tag used in building a local endpoint
% * identifier.
% * 
% * Note that the application cannot (in general) expect to be able to
% * use the full DTN_MAX_ENDPOINT_ID, as there is a chance of overflow
% * when the daemon concats the tag with its own local endpoint id.
% */
struct dtn_service_tag_t {
    char tag[DTN_MAX_ENDPOINT_ID];
};

%
%/**
% * Value for an unspecified registration cookie (i.e. indication that
% * the daemon should allocate a new unique id).
% */
const DTN_REGID_NONE = 0;

%
%/**
% * Registration delivery failure actions
% *     DTN_REG_DROP   - drop bundle if registration not active
% *     DTN_REG_DEFER  - spool bundle for later retrieval
% *     DTN_REG_EXEC   - exec program on bundle arrival
% */
enum dtn_reg_failure_action_t {
    DTN_REG_DROP   = 1,
    DTN_REG_DEFER  = 2,
    DTN_REG_EXEC   = 3
};

%
%/**
% * Registration state.
% */
struct dtn_reg_info_t {
    dtn_endpoint_id_t 		endpoint;
    dtn_reg_id_t		regid;
    dtn_reg_failure_action_t 	failure_action;
    dtn_timeval_t		expiration;
    opaque			script<DTN_MAX_EXEC_LEN>;
};

%
%/**
% * Bundle priority specifier.
% *     COS_BULK      - lowest priority
% *     COS_NORMAL    - regular priority
% *     COS_EXPEDITED - important
% *     COS_RESERVED  - TBD
% */
enum dtn_bundle_priority_t {
    COS_BULK = 0,
    COS_NORMAL = 1,
    COS_EXPEDITED = 2,
    COS_RESERVED = 3
};

%
%/**
% * Bundle delivery option flags. Note that multiple options
% * may be selected for a given bundle.
% *     
% *     DOPTS_NONE           - no custody, etc
% *     DOPTS_CUSTODY        - custody xfer
% *     DOPTS_DELIVERY_RCPT  - end to end delivery (i.e. return receipt)
% *     DOPTS_RECEIVE_RCPT   - per hop arrival receipt
% *     DOPTS_FORWARD_RCPT   - per hop departure receipt
% *     DOPTS_CUSTODY_RCPT   - per custodian receipt
% *     DOPTS_DELETE_RCPT    - request deletion receipt
% */
enum dtn_bundle_delivery_opts_t {
    DOPTS_NONE 		= 0,
    DOPTS_CUSTODY 	= 1,
    DOPTS_DELIVERY_RCPT = 2,
    DOPTS_RECEIVE_RCPT 	= 4,
    DOPTS_FORWARD_RCPT 	= 8,
    DOPTS_CUSTODY_RCPT 	= 16,
    DOPTS_DELETE_RCPT 	= 32
};

%
%/**
% * Bundle metadata.
% */
struct dtn_bundle_spec_t {
    dtn_endpoint_id_t		source;
    dtn_endpoint_id_t		dest;
    dtn_endpoint_id_t		replyto;
    dtn_bundle_priority_t	priority;
    int				dopts;
    dtn_timeval_t		expiration;
};

%
%/**
% * The payload of a bundle can be sent or received either in a file,
% * in which case the payload structure contains the filename, or in
% * memory where the struct has the actual data.
% *
% * Note that there is a limit (DTN_MAX_BUNDLE_MEM) on the maximum size
% * bundle payload that can be sent or received in memory.
% */
enum dtn_bundle_payload_location_t {
    DTN_PAYLOAD_FILE,
    DTN_PAYLOAD_MEM
};

union dtn_bundle_payload_t switch(dtn_bundle_payload_location_t location)
{
 case DTN_PAYLOAD_FILE:
     opaque	filename<DTN_MAX_PATH_LEN>;
 case DTN_PAYLOAD_MEM:
     opaque	buf<DTN_MAX_BUNDLE_MEM>;
};

%
%/**
% * Type definition for a bundle identifier as returned from dtn_send.
% */
struct dtn_bundle_id_t {
    dtn_endpoint_id_t   source;
    u_int               creation_secs;
    u_int               creation_subsecs;
};

%
%/**
% * Bundle authentication data. TBD
% */
struct dtn_bundle_auth_t {
    opaque	blob<DTN_MAX_AUTHDATA>;
};

