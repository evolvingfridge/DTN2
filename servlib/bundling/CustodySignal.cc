/*
 * IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING. By
 * downloading, copying, installing or using the software you agree to
 * this license. If you do not agree to this license, do not download,
 * install, copy or use the software.
 *
 * Intel Open Source License
 *
 * 2006 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 *   Neither the name of the Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <oasys/util/ScratchBuffer.h>
#include "CustodySignal.h"
#include "SDNV.h"

namespace dtn {

void
CustodySignal::create_custody_signal(Bundle*           bundle,
                                     const Bundle*     orig_bundle,
                                     const EndpointID& source_eid,
                                     bool              succeeded,
                                     reason_t          reason)
{
    bundle->source_.assign(source_eid);
    if (orig_bundle->custodian_.equals(EndpointID::NULL_EID())) {
        PANIC("create_custody_signal(*%p): "
              "custody signal cannot be generated to null eid",
              orig_bundle);
    }
    bundle->dest_.assign(orig_bundle->custodian_);
    bundle->replyto_.assign(EndpointID::NULL_EID());
    bundle->custodian_.assign(EndpointID::NULL_EID());
    bundle->is_admin_ = true;

    // use the expiration time from the original bundle
    // XXX/demmer maybe something more clever??
    bundle->expiration_ = orig_bundle->expiration_;

    int sdnv_encoding_len = 0;
    int signal_len = 0;
    
    // we generally don't expect the Custody Signal length to be > 256 bytes
    oasys::ScratchBuffer<u_char*, 256> scratch;
    
    // format of custody signals:
    //
    // 1 byte admin payload type and flags
    // 1 byte status code
    // SDNV   [Fragment Offset (if present)]
    // SDNV   [Fragment Length (if present)]
    // 8 byte Time of custody signal
    // 8 byte Copy of bundle X's Creation Timestamp
    // SDNV   Length of X's source endpoint ID
    // vari   Source endpoint ID of bundle X

    //
    // first calculate the length
    //

    // the non-optional, fixed-length fields above:
    signal_len =  1 + 1 + 8 + 8;

    // the 2 SDNV fragment fields:
    if (orig_bundle->is_fragment_) {
        signal_len += SDNV::encoding_len(orig_bundle->frag_offset_);
        signal_len += SDNV::encoding_len(orig_bundle->orig_length_);
    }

    // the Source Endpoint ID length and value
    signal_len += SDNV::encoding_len(orig_bundle->source_.length()) +
                  orig_bundle->source_.length();

    //
    // now format the buffer
    //
    u_char* bp = scratch.buf(signal_len);
    int len = signal_len;
    
    // Admin Payload Type and flags
    *bp = (BundleProtocol::ADMIN_CUSTODY_SIGNAL << 4);
    if (orig_bundle->is_fragment_) {
        *bp |= BundleProtocol::ADMIN_IS_FRAGMENT;
    }
    bp++;
    len--;
    
    // Success flag and reason code
    *bp++ = ((succeeded ? 1 : 0) << 7) | (reason & 0x7f);
    len--;
    
    // The 2 Fragment Fields
    if (orig_bundle->is_fragment_) {
        sdnv_encoding_len = SDNV::encode(orig_bundle->frag_offset_, bp, len);
        ASSERT(sdnv_encoding_len > 0);
        bp  += sdnv_encoding_len;
        len -= sdnv_encoding_len;
        
        sdnv_encoding_len = SDNV::encode(orig_bundle->orig_length_, bp, len);
        ASSERT(sdnv_encoding_len > 0);
        bp  += sdnv_encoding_len;
        len -= sdnv_encoding_len;
    }   

    // Time field, set to the current time:
    struct timeval now;
    gettimeofday(&now, 0);
    BundleProtocol::set_timestamp(bp, &now);
    len -= sizeof(u_int64_t);
    bp  += sizeof(u_int64_t);

    // Copy of bundle X's Creation Timestamp
    BundleProtocol::set_timestamp(bp, &orig_bundle->creation_ts_);
    len -= sizeof(u_int64_t);
    bp  += sizeof(u_int64_t);
    
    // The Endpoint ID length and data
    sdnv_encoding_len = SDNV::encode(orig_bundle->source_.length(), bp, len);
    ASSERT(sdnv_encoding_len > 0);
    len -= sdnv_encoding_len;
    bp  += sdnv_encoding_len;
    
    ASSERT((u_int)len == orig_bundle->source_.length());
    memcpy(bp, orig_bundle->source_.c_str(), orig_bundle->source_.length());
    
    // 
    // Finished generating the payload
    //
    bundle->payload_.set_data(scratch.buf(), signal_len);
}

bool
CustodySignal::parse_custody_signal(data_t* data,
                                    const u_char* bp, u_int len)
{
    // 1 byte Admin Payload Type + Flags:
    if (len < 1) { return false; }
    data->admin_type_  = (*bp >> 4);
    data->admin_flags_ = *bp & 0xf;
    bp++;
    len--;

    // validate the admin type
    if (data->admin_type_ != BundleProtocol::ADMIN_CUSTODY_SIGNAL) {
        return false;
    }

    // Success flag and reason code
    if (len < 1) { return false; }
    data->succeeded_ = (*bp >> 7);
    data->reason_    = (*bp & 0x7f);
    bp++;
    len--;
    
    // Fragment SDNV Fields (offset & length), if present:
    if (data->admin_flags_ & BundleProtocol::ADMIN_IS_FRAGMENT)
    {
        int sdnv_bytes = SDNV::decode(bp, len, &data->orig_frag_offset_);
        if (sdnv_bytes == -1) { return false; }
        bp  += sdnv_bytes;
        len -= sdnv_bytes;
        
        sdnv_bytes = SDNV::decode(bp, len, &data->orig_frag_length_);
        if (sdnv_bytes == -1) { return false; }
        bp  += sdnv_bytes;
        len -= sdnv_bytes;
    }

    // The signal timestamp
    if (len < sizeof(u_int64_t)) { return false; }
    BundleProtocol::get_timestamp(&data->custody_signal_tv_, bp);
    bp  += sizeof(u_int64_t);
    len -= sizeof(u_int64_t);

    // Copy of the bundle's creation timestamp
    if (len < sizeof(u_int64_t)) { return false; }
    BundleProtocol::get_timestamp(&data->orig_creation_tv_, bp);
    bp  += sizeof(u_int64_t);
    len -= sizeof(u_int64_t);

    // Source Endpoint ID of Bundle
    u_int64_t EID_len;
    int num_bytes = SDNV::decode(bp, len, &EID_len);
    if (num_bytes == -1) { return false; }
    bp  += num_bytes;
    len -= num_bytes;

    if (len != EID_len) { return false; }
    bool ok = data->orig_source_eid_.assign(std::string((const char*)bp, len));
    if (!ok) {
        return false;
    }
    
    return true;
}

} // namespace dtn