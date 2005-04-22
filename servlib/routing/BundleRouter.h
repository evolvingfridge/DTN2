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
#ifndef _BUNDLE_ROUTER_H_
#define _BUNDLE_ROUTER_H_

#include <vector>
#include <oasys/debug/Logger.h>
#include <oasys/thread/Thread.h>
#include <oasys/util/StringUtils.h>

#include "bundling/BundleEvent.h"
#include "bundling/BundleTuple.h"

namespace dtn {

class BundleActions;
class BundleConsumer;
class BundleRouter;
class RouteTable;
class StringBuffer;

/**
 * Typedef for a list of bundle routers.
 */
typedef std::vector<BundleRouter*> BundleRouterList;

/**
 * The BundleRouter is the main decision maker for all routing
 * decisions related to bundles.
 *
 * It receives events from the BundleDaemon having been posted by
 * other components. These events include all operations and
 * occurrences that may affect bundle delivery, including new bundle
 * arrival, contact arrival, timeouts, etc.
 *
 * In response to each event the router may call one of the action
 * functions implemented by the BundleDaemon. Note that to support the
 * simulator environment, all interactions with the rest of the system
 * should go through the singleton BundleAction classs.
 *
 * To support prototyping of different routing protocols and
 * frameworks, the base class has a list of prospective BundleRouter
 * implementations, and at boot time, one of these is selected as the
 * active routing algorithm. As new algorithms are added to the
 * system, new cases should be added to the "create_router" function.
 */
class BundleRouter : public oasys::Logger {
public:
    /**
     * Factory method to create the correct subclass of BundleRouter
     * for the registered algorithm type.
     */
    static BundleRouter* create_router(const char* type);

    /**
     * Config variables. These must be static since they're set by the
     * config parser before the router object is created. At
     * initialization time, the local_tuple and local_regions
     * variables are propagated into the actual router instance.
     */
    static struct config_t {
        std::string         type_;
        oasys::StringVector local_regions_;
        BundleTuple         local_tuple_;
    } Config;
    
    /**
     * Constructor
     */
    BundleRouter();

    /**
     * Destructor
     */
    virtual ~BundleRouter();

    /**
     * Accessor for the route table.
     */
    const RouteTable* route_table() { return route_table_; }

    /**
     * The monster routing decision function that is called in
     * response to all received events.
     *
     * To actually effect actions, this function should populate the
     * given action list with all forwarding decisions. The
     * BundleDaemon then takes these instructions and causes them
     * to happen.
     *
     * The base class implementation does a dispatch on the event type
     * and calls the appropriate default handler function.
     */
    virtual void handle_event(BundleEvent* event);

    /// XXX/demmer temp for testing fragmentation
    static size_t proactive_frag_threshold_;

    /**
     * Accessor for the vector of local regions.
     */
    oasys::StringVector* local_regions() { return &local_regions_; }

    /**
     * Accessor for the local tuple.
     */
    const BundleTuple& local_tuple() { return local_tuple_; }

    /**
     * Assignment function for the local tuple string.
     */
    void set_local_tuple(const char* tuple_str) {
        local_tuple_.assign(tuple_str);
    }
    
protected:
    /**
     * Default event handler for new bundle arrivals.
     *
     * Queue the bundle on the pending delivery list, and then
     * searches through the route table to find any matching next
     * contacts, filling in the action list with forwarding decisions.
     */
    virtual void handle_bundle_received(BundleReceivedEvent* event);
    
    /**
     * Default event handler when bundles are transmitted.
     *
     * If the bundle was only partially transmitted, this calls into
     * the fragmentation module to create a new bundle fragment and
     * enqeues the new fragment on the appropriate list.
     */
    virtual void handle_bundle_transmitted(BundleTransmittedEvent* event);

    /**
     * Default event handler when a new application registration
     * arrives.
     *
     * Adds an entry to the route table for the new registration, then
     * walks the pending list to see if any bundles match the
     * registration.
     */
    virtual void handle_registration_added(RegistrationAddedEvent* event);
    
    /**
     * Default event handler when a new contact is up.
     */
    virtual void handle_contact_up(ContactUpEvent* event);
    
    /**
     * Default event handler when a contact is down.
     */
    virtual void handle_contact_down(ContactDownEvent* event);

    /**
     * Default event handler when a new link is created.
     */
    virtual void handle_link_created(LinkCreatedEvent* event);
    
    /**
     * Default event handler when a link is deleted.
     */
    virtual void handle_link_deleted(LinkDeletedEvent* event);

    /**
     * Default event handler when link becomes available
     */
    virtual void handle_link_available(LinkAvailableEvent* event);    

    /**
     * Default event handler when a link is unavailable
     */
    virtual void handle_link_unavailable(LinkUnavailableEvent* event);

    /**
     * Default event handler when reassembly is completed. For each
     * bundle on the list, check the pending count to see if the
     * fragment can be deleted.
     */
    virtual void handle_reassembly_completed(ReassemblyCompletedEvent* event);
    
    /**
     * Default event handler when a new route is added by the command
     * or management interface.
     *
     * Adds an entry to the route table, then walks the pending list
     * to see if any bundles match the new route.
     */
    virtual void handle_route_add(RouteAddEvent* event);

    /**
     * Add an action to forward a bundle to a next hop route, making
     * sure to do reassembly if the forwarding action specifies as
     * such.
     */
    virtual void fwd_to_nexthop(Bundle* bundle, RouteEntry* nexthop);
     
    
    /**
     * Call fwd_to_matching for all matching entries in the routing
     * table.
     *
     * Note that if the include_local flag is set, then local routes
     * (i.e. registrations) are included in the list.
     *
     * Returns the number of matches found and assigned.
     */
    virtual int fwd_to_matching(Bundle* bundle, 
                                bool include_local);

    /**
     * Called whenever a new consumer (i.e. registration or contact)
     * arrives. This walks the list of all pending bundles, forwarding
     * all matches to the new contact.
     */
    virtual void new_next_hop(const BundleTuplePattern& dest,
                              BundleConsumer* next_hop);

    /**
     * Delete the given bundle from the pending list (assumes the
     * pending count is zero).
     */
    void delete_from_pending(Bundle* bundle);


    /**
     * Add a route entry to the routing table. 
     */
    void add_route(RouteEntry *entry);

    /// The set of local regions that this router is configured as "in".
    oasys::StringVector local_regions_;

    /**
     * The default tuple for reaching this router, used for bundle
     * status reports, etc. Note that the region must be one of the
     * locally configured regions.
     */
    BundleTuple local_tuple_;
    
    /// The routing table
    RouteTable* route_table_;

    /// The list of all bundles still pending delivery
    BundleList* pending_bundles_;

    /// The list of all bundles that I have custody of
    BundleList* custody_bundles_;

    /// The actions interface, set by the BundleDaemon when the router
    /// is initialized.
    BundleActions* actions_;
};

} // namespace dtn

#endif /* _BUNDLE_ROUTER_H_ */
