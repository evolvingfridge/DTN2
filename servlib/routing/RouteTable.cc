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

#include <oasys/util/StringBuffer.h>

#include "RouteTable.h"
#include "bundling/BundleConsumer.h"

namespace dtn {

/**
 * RouteEntry constructor.
 */
RouteEntry::RouteEntry(const BundleTuplePattern& pattern,
                       BundleConsumer* next_hop,
                       bundle_fwd_action_t action)
    : pattern_(pattern),
      action_(action),
      next_hop_(next_hop),
      info_(NULL)
{
}

/**
 * RouteEntry destructor
 */
RouteEntry::~RouteEntry()
{
    if (info_)
        delete info_;
}

/**
 * Constructor
 */
RouteTable::RouteTable()
    : Logger("/route/table")
{
}

/**
 * Destructor
 */
RouteTable::~RouteTable()
{
}

/**
 * Add a route entry.
 */
bool
RouteTable::add_entry(RouteEntry* entry)
{
    // XXC/demmer check for duplicates?
    
    
    log_debug("add_route %s -> %s (%s)",
              entry->pattern_.c_str(),
              entry->next_hop_->dest_str(),
              bundle_fwd_action_toa(entry->action_));
    
    route_table_.insert(entry);
    
    return true;
}
    
/**
 * Remove a route entry.
 */
bool
RouteTable::del_entry(const BundleTuplePattern& dest,
                      BundleConsumer* next_hop)
{
    RouteEntrySet::iterator iter;
    RouteEntry* entry;

    for (iter = route_table_.begin(); iter != route_table_.end(); ++iter) {
        entry = *iter;

        if (entry->pattern_.equals(dest) && entry->next_hop_ == next_hop) {
            log_debug("del_route %s -> %s",
                      dest.c_str(), next_hop->dest_str());

            route_table_.erase(iter);
            return true;
        }
    }    

    log_debug("del_route %s -> %s: no match!",
              dest.c_str(), next_hop->dest_str());
    return false;
}

/**
 * Fill in the entry_set with the list of all entries whose
 * patterns match the given tuple.
 *
 * @return the count of matching entries
 */
size_t
RouteTable::get_matching(const BundleTuple& tuple,
                         RouteEntrySet* entry_set) const
{
    RouteEntrySet::iterator iter;
    RouteEntry* entry;
    size_t count = 0;

    log_debug("get_matching %s", tuple.c_str());
    
    for (iter = route_table_.begin(); iter != route_table_.end(); ++iter) {
        entry = *iter;

        log_debug("check entry %s -> %s (%s)",
                  entry->pattern_.c_str(),
                  entry->next_hop_->dest_str(),
                  bundle_fwd_action_toa(entry->action_));
            
        if (entry->pattern_.match(tuple)) {
            ++count;
            
            log_debug("match entry %s -> %s (%s)",
                      entry->pattern_.c_str(),
                      entry->next_hop_->dest_str(),
                      bundle_fwd_action_toa(entry->action_));

            entry_set->insert(entry);
        }
    }

    log_debug("get_matching %s done, %d match(es)", tuple.c_str(), count);
    return count;
}

/**
 * Dump the routing table.
 */
void
RouteTable::dump(oasys::StringBuffer* buf) const
{
    buf->append("route table:\n");

    RouteEntrySet::iterator iter;
    for (iter = route_table_.begin(); iter != route_table_.end(); ++iter) {
        RouteEntry* entry = *iter;
        buf->appendf("\t%s -> %s (%s) (%s)\n",
                     entry->pattern_.c_str(),
                     entry->next_hop_->dest_str(),
                     entry->next_hop_->type_str(),
                     bundle_fwd_action_toa(entry->action_));
    }
}

} // namespace dtn
