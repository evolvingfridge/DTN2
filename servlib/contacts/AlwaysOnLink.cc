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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "AlwaysOnLink.h"
#include "bundling/BundleDaemon.h"

namespace dtn {

AlwaysOnLink::AlwaysOnLink(std::string name, ConvergenceLayer* cl,
                           const char* nexthop)
    : Link(name, ALWAYSON, cl, nexthop)
{
    set_state(AVAILABLE);
}

void
AlwaysOnLink::set_initial_state()
{
    BundleDaemon::post(
        new LinkStateChangeRequest(LinkRef(this, "AlwaysOnLink"),
                                   Link::OPEN, ContactEvent::USER));
}

} // namespace dtn

