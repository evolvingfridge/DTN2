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

#include <stdlib.h>

#include "BundleRouter.h"
#include "bundling/BundleDaemon.h"

#include "StaticBundleRouter.h"
#include "FloodBundleRouter.h"
#include "NeighborhoodRouter.h"
#include "LinkStateRouter.h"

namespace dtn {

BundleRouter::config_t BundleRouter::Config;



/*
 * Note by sushant:
 * Reasons to open a link (which is closed) in the default router implementation:
 * (note, link can be opened only if link is available)
 * 1. Link available event and Link queue has > 0 messages
 * 2. Contact_down event and link is available and queue has > 0 messages
 * 3. New message on link queue and link is available
 *
 * TODO: Peer routing semantics
 * When Link Available event arrives transfer message from peer queue
 * to Link queue
 * Also, if new message on peer queue and atleast one link is available
 */

/**
 * Factory method to create the correct subclass of BundleRouter
 * for the registered algorithm type.
 */
BundleRouter*
BundleRouter::create_router(const char* type)
{
    if (strcmp(type, "static") == 0) {
        return new StaticBundleRouter();
    }
    else if (strcmp(type, "neighborhood") == 0) {
        return new NeighborhoodRouter();
    }
    else if (strcmp(type, "flood") == 0) {
        return new FloodBundleRouter();
    }
    else if (strcmp(type, "linkstate") == 0) {
        return new LinkStateRouter();
    }    
    else {
        PANIC("unknown type %s for router", type);
    }
}

/**
 * Constructor.
 */
BundleRouter::BundleRouter(const std::string& name)
    : name_(name), local_tuple_(Config.local_tuple_)
{
    if (local_tuple_.length() == 0) {
        local_tuple_.assign("bundles://internet/host://localhost");
    }

    logpathf("/route/%s", name.c_str());
    
    log_debug("%s router initializing (local_tuple %s)",
              name_.c_str(), local_tuple_.c_str());
    
    actions_ = BundleDaemon::instance()->actions();

    // XXX/demmer maybe change this?
    pending_bundles_ = BundleDaemon::instance()->pending_bundles();
    custody_bundles_ = BundleDaemon::instance()->custody_bundles();
}

/* 
 * Virtual initialization function that's called after all the main
 * components are set up.
 */
void
BundleRouter::initialize()
{
}

/**
 * Destructor
 */
BundleRouter::~BundleRouter()
{
    NOTIMPLEMENTED;
}

} // namespace dtn
