#ifndef _BUNDLE_PAYLOAD_H_
#define _BUNDLE_PAYLOAD_H_

#include <string>
#include "storage/Serialize.h"

/**
 * The representation of a bundle payload.
 *
 * This is abstracted into a separate class to allow the daemon to
 * separately manage the serialization of header information from the
 * payload.
 */
class BundlePayload : public SerializableObject {
public:
    BundlePayload();
    virtual ~BundlePayload();
    
    /**
     * The payload length;
     */
    size_t length() { return data_.length(); }

    /**
     * The actual payload data.
     */
    const char* data() { return data_.data(); }
    
    /**
     * Virtual from SerializableObject
     */
    virtual void serialize(SerializeAction* a);

protected:
    std::string data_;	/// the actual payload data
};

#endif /* _BUNDLE_PAYLOAD_H_ */
