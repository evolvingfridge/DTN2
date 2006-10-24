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

#ifndef _CONTACT_H_
#define _CONTACT_H_

#include <oasys/debug/DebugUtils.h>
#include <oasys/debug/Formatter.h>
#include <oasys/util/Ref.h>
#include <oasys/util/RefCountedObject.h>

namespace dtn {

class Bundle;
class BundleList;
class ConvergenceLayer;
class CLInfo;
class Link;

/**
 * Encapsulation of an active connection to a next-hop DTN contact.
 * This is basically a repository for any state about the contact
 * opportunity including start time, estimations for bandwidth or
 * latency, etc.
 *
 * It also contains the CLInfo slot for the convergence layer to put
 * any state associated with the active connection.
 *
 * Since the contact object may be used by multiple threads in the
 * case of a connection-oriented convergence layer, and because the
 * object is intended to be deleted when the contact opportunity ends,
 * all object instances are reference counted and will be deleted when
 * the last reference is removed.
 */
class Contact : public oasys::RefCountedObject,
                public oasys::Logger
{
public:
    /**
     * Constructor
     */
    Contact(Link* link);

private:
    /**
     * Destructor -- private since the class is reference counted and
     * therefore is never explicitly deleted.
     */ 
    virtual ~Contact();
    friend class oasys::RefCountedObject;

public:
    /**
     * Store the convergence layer state associated with the contact.
     */
    void set_cl_info(CLInfo* cl_info)
    {
        ASSERT((cl_info_ == NULL && cl_info != NULL) ||
               (cl_info_ != NULL && cl_info == NULL));
        
        cl_info_ = cl_info;
    }
    
    /**
     * Accessor to the convergence layer info.
     */
    CLInfo* cl_info() { return cl_info_; }
    
    /**
     * Accessor to the link
     */
    Link* link() { return link_; }

    /**
     * Virtual from formatter
     */
    int format(char* buf, size_t sz) const;

    /// Time when the contact begin
    struct timeval start_time_;

    /// Contact duration (0 if unknown)
    u_int32_t duration_ms_;

    /// Approximate bandwidth
    u_int32_t bps_;
    
    /// Approximate latency
    u_int32_t latency_ms_;

protected:
    Link* link_ ; 	///< Pointer to parent link on which this
    			///  contact exists
    
    CLInfo* cl_info_;	///< convergence layer specific info
};

/**
 * Typedef for a reference on a contact.
 */
typedef oasys::Ref<Contact> ContactRef;

} // namespace dtn

#endif /* _CONTACT_H_ */
