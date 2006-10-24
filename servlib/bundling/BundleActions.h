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

#ifndef _BUNDLE_ACTIONS_H_
#define _BUNDLE_ACTIONS_H_

#include <vector>
#include <oasys/debug/Log.h>
#include "ForwardingInfo.h"
#include "BundleProtocol.h"

namespace dtn {

class Bundle;
class BundleList;
class CustodyTimerSpec;
class EndpointID;
class Interface;
class Link;
class RouterInfo;

/**
 * Intermediary class that provides the interface that is exposed to
 * routers for the rest of the system. All functions are virtual since
 * the simulator overrides them to provide equivalent functionality
 * (in the simulated environment).
 */
class BundleActions : public oasys::Logger {
public:
    BundleActions() : Logger("BundleActions", "/dtn/bundle/actions") {}
    virtual ~BundleActions() {}
    
    /**
     * Open a link for bundle transmission. The link should be in
     * state AVAILABLE for this to be called.
     *
     * This may either immediately open the link in which case the
     * link's state will be OPEN, or post a request for the
     * convergence layer to complete the session initiation in which
     * case the link state is OPENING.
     */
    virtual void open_link(Link* link);

    /**
     * Open a link for bundle transmission. The link should be in an
     * open state for this to be called.
     */
    virtual void close_link(Link* link);
    
    /**
     * Initiate transmission of a bundle out the given link. The link
     * must already be open.
     *
     * @param bundle		the bundle
     * @param link		the link on which to send the bundle
     * @param action		the forwarding action that was taken
     * @param custody_timer	custody timer specification
     *
     * @return true if the transmission was successfully initiated
     */
    virtual bool send_bundle(Bundle* bundle, Link* link,
                             ForwardingInfo::action_t action,
                             const CustodyTimerSpec& custody_timer);
    
    /**
     * Attempt to cancel transmission of a bundle on the given link.
     *
     * @param bundle		the bundle
     * @param link		the link on which the bundle was queued
     *
     * @return			true if successful
     */
    virtual bool cancel_bundle(Bundle* bundle, Link* link);

    /**
     * Inject a new bundle into the core system, which places it in
     * the pending bundles list as well as in the persistent store.
     * This is typically used by routing algorithms that need to
     * generate their own bundles for distribuing route announcements.
     * It does not, therefore, generate a BundleReceivedEvent.
     *
     * @param bundle		the new bundle
     */
    virtual void inject_bundle(Bundle* bundle);

    /**
     * Attempt to delete a bundle from the system.
     *
     * @param bundle       The bundle
     * @param reason       Bundle Status Report reason code
     * @param log_on_error Set to false to suppress error logging
     *
     * @return      true if successful
     */
    virtual bool delete_bundle(Bundle* bundle,
                               BundleProtocol::status_report_reason_t
                                 reason = BundleProtocol::REASON_NO_ADDTL_INFO,
                               bool log_on_error = true);

protected:
    // The protected functions (exposed only to the BundleDaemon) and
    // serve solely for the simulator abstraction
    friend class BundleDaemon;
    
    /**
     * Add the given bundle to the data store.
     */
    virtual void store_add(Bundle* bundle);

    /**
     * Update the on-disk version of the given bundle, after it's
     * bookkeeping or header fields have been modified.
     */
    virtual void store_update(Bundle* bundle);

    /**
     * Remove the given bundle from the data store.
     */
    virtual void store_del(Bundle* bundle);
};

} // namespace dtn

#endif /* _BUNDLE_ACTIONS_H_ */
