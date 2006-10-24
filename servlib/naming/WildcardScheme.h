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

#ifndef _WILDCARD_SCHEME_H_
#define _WILDCARD_SCHEME_H_

#include <oasys/util/Singleton.h>

#include "Scheme.h"

namespace dtn {

class WildcardScheme : public Scheme, public oasys::Singleton<WildcardScheme> {
public:
    /**
     * Validate that the given ssp is legitimate for this scheme. If
     * the 'is_pattern' paraemeter is true, then the ssp is being
     * validated as an EndpointIDPattern.
     *
     * @return true if valid
     */
    virtual bool validate(const std::string& ssp, bool is_pattern = false);
    
    /**
     * Match the pattern to the endpoint id in a scheme-specific
     * manner.
     */
    virtual bool match(const EndpointIDPattern& pattern,
                       const EndpointID& eid);
    
private:
    friend class oasys::Singleton<WildcardScheme>;
    WildcardScheme() {}
};

} // namespace dtn

#endif /* _WILDCARD_SCHEME_H_ */
