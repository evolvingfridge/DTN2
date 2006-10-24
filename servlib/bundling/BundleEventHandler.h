/*
 *    Copyright 2005-2006 Intel Corporation
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

#ifndef _BUNDLE_EVENT_HANDLER_H_
#define _BUNDLE_EVENT_HANDLER_H_

#include <oasys/debug/Log.h>

#include "BundleEvent.h"

namespace dtn {

/**
 * Both the BundleDaemon and all the BundleRouter classes need to
 * process the various types of BundleEvent that are generated by the
 * rest of the system. This class provides that abstraction plus a
 * useful dispatching function for event-specific handlers.
 */
class BundleEventHandler : public oasys::Logger {
public:
    /**
     * Pure virtual event handler function.
     */
    virtual void handle_event(BundleEvent* event) = 0;

protected:
    /**
     * Constructor -- protected since this class shouldn't ever be
     * instantiated directly.
     */
    BundleEventHandler(const char* classname,
                       const char* logpath)
        : oasys::Logger(classname, logpath) {}
    
    /**
     * Destructor -- Needs to be defined virtual to be sure that
     * derived classes get a chance to clean up their stuff on removal.
     */
    virtual ~BundleEventHandler() {}

    /** 
     * Dispatch the event by type code to one of the event-specific
     * handler functions below.
     */
    void dispatch_event(BundleEvent* event);
    
    /**
     * Default event handler for new bundle arrivals.
     */
    virtual void handle_bundle_received(BundleReceivedEvent* event);
    
    /**
     * Default event handler when bundles are transmitted.
     */
    virtual void handle_bundle_transmitted(BundleTransmittedEvent* event);
    
    /**
     * Default event handler when a bundle transmission fails.
     */
    virtual void handle_bundle_transmit_failed(BundleTransmitFailedEvent* event);
    
    /**
     * Default event handler when bundles are locally delivered.
     */
    virtual void handle_bundle_delivered(BundleDeliveredEvent* event);
    
    /**
     * Default event handler when bundles expire.
     */
    virtual void handle_bundle_expired(BundleExpiredEvent* event);

    /**
     * Default event handler when bundles are free (i.e. no more
     * references).
     */
    virtual void handle_bundle_free(BundleFreeEvent* event);

    /**
     * Default event handler when a new application registration
     * arrives.
     */
    virtual void handle_registration_added(RegistrationAddedEvent* event);
    
    /**
     * Default event handler when a registration is removed.
     */
    virtual void handle_registration_removed(RegistrationRemovedEvent* event);
    
    /**
     * Default event handler when a registration expires.
     */
    virtual void handle_registration_expired(RegistrationExpiredEvent* event);
    
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
     * Default event handler for link state change requests.
     */
    virtual void handle_link_state_change_request(LinkStateChangeRequest* req);

    /**
     * Default event handler when reassembly is completed.
     */
    virtual void handle_reassembly_completed(ReassemblyCompletedEvent* event);
    
    /**
     * Default event handler when a new route is added by the command
     * or management interface.
     */
    virtual void handle_route_add(RouteAddEvent* event);
    
    /**
     * Default event handler when a route is deleted by the command
     * or management interface.
     */
    virtual void handle_route_del(RouteDelEvent* event);

    /**
     * Default event handler when custody signals are received.
     */
    virtual void handle_custody_signal(CustodySignalEvent* event);
    
    /**
     * Default event handler when custody transfer timers expire
     */
    virtual void handle_custody_timeout(CustodyTimeoutEvent* event);
    
    /**
     * Default event handler for shutdown requests.
     */
    virtual void handle_shutdown_request(ShutdownRequest* event);

    /**
     * Default event handler for status requests.
     */
    virtual void handle_status_request(StatusRequest* event);
};

} // namespace dtn

#endif /* _BUNDLE_EVENT_HANDLER_H_ */
