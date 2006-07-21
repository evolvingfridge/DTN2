/*
 * IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING. By
 * downloading, copying, installing or using the software you agree to
 * this license. If you do not agree to this license, do not download,
 * install, copy or use the software.
 * 
 * Intel Open Source License 
 * 
 * Copyright (c) 2004 Intel Corporation. All rights reserved. 
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
#include <oasys/util/OptParser.h>

#include "Link.h"
#include "ContactManager.h"
#include "AlwaysOnLink.h"
#include "OndemandLink.h"
#include "ScheduledLink.h"
#include "OpportunisticLink.h"

#include "bundling/BundleDaemon.h"
#include "bundling/BundleEvent.h"
#include "conv_layers/ConvergenceLayer.h"
#include "naming/EndpointIDOpt.h"

namespace dtn {

//----------------------------------------------------------------------
/// Default parameters, values set in ParamCommand
Link::Params Link::default_params_ = {
    default_params_.mtu_                = 0,
    default_params_.min_retry_interval_ = 5,
    default_params_.max_retry_interval_ = 10 * 60
};

//----------------------------------------------------------------------
Link*
Link::create_link(const std::string& name, link_type_t type,
                  ConvergenceLayer* cl, const char* nexthop,
                  int argc, const char* argv[],
                  const char** invalid_argp)
{
    Link* link;
    switch(type) {
    case ALWAYSON: 	link = new AlwaysOnLink(name, cl, nexthop); break;
    case ONDEMAND: 	link = new OndemandLink(name, cl, nexthop); break;
    case SCHEDULED: 	link = new ScheduledLink(name, cl, nexthop); break;
    case OPPORTUNISTIC: link = new OpportunisticLink(name, cl, nexthop); break;
    default: 		PANIC("bogus link_type_t");
    }

    // hook for the link subclass that parses any arguments and shifts
    // argv appropriately
    int count = link->parse_args(argc, argv, invalid_argp);
    if (count == -1) {
        delete link;
        return NULL;
    }

    argc -= count;

    // notify the convergence layer, which parses the rest of the
    // arguments
    ASSERT(link->clayer_);
    if (!link->clayer_->init_link(link, argc, argv)) {
        delete link;
        return NULL;
    }
    
    link->logf(oasys::LOG_INFO, "new link *%p", link);

    // now dispatch to the subclass for any initial state events that
    // need to be posted. this needs to be done after all the above is
    // completed to avoid potential race conditions if the core of the
    // system tries to use the link before its completely created
    link->set_initial_state();

    return link;
}

//----------------------------------------------------------------------
Link::Link(const std::string& name, link_type_t type,
           ConvergenceLayer* cl, const char* nexthop)
    :  Logger("Link", "/dtn/link/%s", name.c_str()),
       type_(type),
       state_(UNAVAILABLE),
       nexthop_(nexthop),
       name_(name),
       reliable_(false),
       contact_("Link"),
       clayer_(cl),
       cl_info_(NULL),
       remote_eid_(EndpointID::NULL_EID())
{
    ASSERT(clayer_);

    params_ = default_params_;
    retry_interval_ = params_.min_retry_interval_;

    memset(&stats_, 0, sizeof(Stats));
}

//----------------------------------------------------------------------
Link::Link(const oasys::Builder&)
    : Logger("Link", "/dtn/link/UNKNOWN!!!"),
      type_(LINK_INVALID),
      state_(UNAVAILABLE),
      nexthop_(""),
      name_(""),
      reliable_(false),
      contact_("Link"),
      clayer_(NULL),
      cl_info_(NULL),
      remote_eid_(EndpointID::NULL_EID())
{
}

//----------------------------------------------------------------------
void
Link::serialize(oasys::SerializeAction* a)
{
    std::string cl_name;
    std::string type_str;

    if (a->action_code() == oasys::Serialize::UNMARSHAL) {
        a->process("type",     &type_str);
        type_ = str_to_link_type(type_str.c_str());
        ASSERT(type_ != LINK_INVALID);
    } else {
        type_str = link_type_to_str(type_);
        a->process("type",     &type_str);
    }
    
    a->process("nexthop",  &nexthop_);
    a->process("name",     &name_);
    a->process("reliable", &reliable_);

    if (a->action_code() == oasys::Serialize::UNMARSHAL) {
        a->process("clayer", &cl_name);
        clayer_ = ConvergenceLayer::find_clayer(cl_name.c_str());
        ASSERT(clayer_);
    } else {
        cl_name = clayer_->name();
        a->process("clayer", &cl_name);
    }

    a->process("remote_eid",         &remote_eid_);
    a->process("min_retry_interval", &params_.min_retry_interval_);
    a->process("max_retry_interval", &params_.max_retry_interval_);

    if (a->action_code() == oasys::Serialize::UNMARSHAL) {
        logpathf("/dtn/link/%s", name_.c_str());
    }
}

//----------------------------------------------------------------------
int
Link::parse_args(int argc, const char* argv[], const char** invalidp)
{
    oasys::OptParser p;

    p.addopt(new dtn::EndpointIDOpt("remote_eid", &remote_eid_));
    p.addopt(new oasys::BoolOpt("reliable", &reliable_));
    p.addopt(new oasys::UIntOpt("mtu", &params_.mtu_));
    p.addopt(new oasys::UIntOpt("min_retry_interval",
                                &params_.min_retry_interval_));
    p.addopt(new oasys::UIntOpt("max_retry_interval",
                                &params_.max_retry_interval_));

    int ret = p.parse_and_shift(argc, argv, invalidp);
    if (ret == -1) {
        return -1;
    }

    if (params_.min_retry_interval_ == 0 ||
        params_.max_retry_interval_ == 0)
    {
        *invalidp = "invalid retry interval";
        return -1;
    }

    return ret;
}

//----------------------------------------------------------------------
void
Link::set_initial_state()
{
}

//----------------------------------------------------------------------
Link::~Link()
{
    /*
     * Once they're created, links are never actually deleted.
     * However, if there's a misconfiguration, then init_link may
     * delete the link, so we don't want to PANIC here.
     *
     * Note also that the destructor of the class is protected so
     * we're (relatively) sure this constraint does hold.
     */
    ASSERT(!isopen());
    ASSERT(cl_info_ == NULL);
}

//----------------------------------------------------------------------
void
Link::set_state(state_t new_state)
{
    log_debug("set_state %s -> %s",
              state_to_str(state_), state_to_str(new_state));

#define ASSERT_STATE(condition)                         \
    if (!(condition)) {                                 \
        log_err("set_state %s -> %s: expected %s",      \
                state_to_str(state_),                   \
                state_to_str(new_state),                \
                #condition);                            \
    }

    switch(new_state) {
    case UNAVAILABLE:
        break; // any old state is valid

    case AVAILABLE:
        ASSERT_STATE(state_ == UNAVAILABLE);
        break;

    case OPENING:
        ASSERT_STATE(state_ == AVAILABLE || state_ == UNAVAILABLE);
        break;
        
    case OPEN:
        ASSERT_STATE(state_ == OPENING || state_ == BUSY ||
                     state_ == UNAVAILABLE /* for opportunistic links */);
        break;

    case BUSY:
        ASSERT_STATE(state_ == OPEN);
        break;
    
    default:
        NOTREACHED;
    }
#undef ASSERT_STATE

    state_ = new_state;
}

//----------------------------------------------------------------------
void
Link::open()
{
    log_debug("Link::open");

    if (state_ != AVAILABLE) {
        log_crit("Link::open in state %s: expected state AVAILABLE",
                 state_to_str(state_));
        return;
    }

    set_state(OPENING);

    // tell the convergence layer to establish a new session however
    // it needs to, it will set the Link state to OPEN and post a
    // ContactUpEvent when it has done the deed
    ASSERT(contact_ == NULL);
    contact_ = new Contact(this);
    clayer()->open_contact(contact_);
}
    
//----------------------------------------------------------------------
void
Link::close()
{
    log_debug("Link::close");

    // we should always be open, therefore we must have a contact
    if (contact_ == NULL) {
        log_err("Link::close with no contact");
        return;
    }
    
    // Kick the convergence layer to close the contact and make sure
    // it cleaned up its state
    clayer()->close_contact(contact_);
    ASSERT(contact_->cl_info() == NULL);

    // Remove the reference from the link, which will clean up the
    // object eventually
    contact_ = NULL;

    log_debug("Link::close complete");
}

//----------------------------------------------------------------------
int
Link::format(char* buf, size_t sz) const
{
    return snprintf(buf, sz, "%s [%s %s %s %s]",
                    name(), nexthop(), remote_eid_.c_str(),
                    link_type_to_str(type_), state_to_str(state_));
}

//----------------------------------------------------------------------
void
Link::dump_stats(oasys::StringBuffer* buf)
{
    buf->appendf("%u bundles_transmitted -- "
                 "%u bytes_transmitted -- "
                 "%u bundles_inflight -- "
                 "%u bytes_inflight ",
                 stats_.bundles_transmitted_,
                 stats_.bytes_transmitted_,
                 stats_.bundles_inflight_,
                 stats_.bytes_inflight_);
}

} // namespace dtn
