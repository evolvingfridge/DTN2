#ifndef _BUNDLE_EVENT_H_
#define _BUNDLE_EVENT_H_

#include "BundleRef.h"
#include "BundleList.h"

/**
 * All signaling from various components to the routing layer is done
 * via the Bundle Event message abstraction. This file defines the
 * event type codes and corresponding classes.
 */

class Bundle;
class BundleConsumer;
class Contact;
class Registration;
class RouteEntry;
class Link;

/**
 * Type codes for events.
 */
typedef enum {
    BUNDLE_RECEIVED = 0x1,	///< New bundle arrival
    BUNDLE_TRANSMITTED,		///< Bundle or fragment successfully sent
    BUNDLE_EXPIRED,		///< Bundle expired
    BUNDLE_FORWARD_TIMEOUT, 	///< A Mapping timed out
        
    CONTACT_UP,		        ///< Contact is up
    CONTACT_DOWN,		///< Contact abnormally terminated

    LINK_CREATED,		///< Link is created into the system
    LINK_DELETED,		///< Link is deleted from the system
    LINK_AVAILABLE,		///< Link is available 
    LINK_UNAVAILABLE,		///< Link is unavailable
 
    REGISTRATION_ADDED,		///< New registration arrived
    REGISTRATION_REMOVED,	///< Registration removed
    REGISTRATION_EXPIRED,	///< Registration expired
    REASSEMBLY_COMPLETED,	///< Reassembly completed

    // These events are injected from the management interface and/or
    // the console
    ROUTE_ADD,			///< Add a new entry to the route table
    ROUTE_DEL,			///< Remove an entry from the route table

} event_type_t;

/**
 * Conversion function from an event to a string.
 */
inline const char*
event_to_str(event_type_t event)
{
    switch(event) {

    case BUNDLE_RECEIVED:        return "BUNDLE_RECEIVED";            
    case BUNDLE_TRANSMITTED:     return "BUNDLE_TRANSMITTED";		
    case BUNDLE_EXPIRED:         return "BUNDLE_EXPIRED";
    case BUNDLE_FORWARD_TIMEOUT: return "BUNDLE_FORWARD_TIMEOUT";	

    case  CONTACT_UP:            return "CONTACT_UP";		        
    case  CONTACT_DOWN:          return "CONTACT_DOWN";		
        
    case LINK_CREATED:           return "LINK_CREATED";		
    case LINK_DELETED:           return "LINK_DELETED";		
    case LINK_AVAILABLE:         return "LINK_AVAILABLE";		 
    case LINK_UNAVAILABLE:       return "LINK_UNAVAILABLE";		
 
    case REGISTRATION_ADDED:     return "REGISTRATION_ADDED";		
    case REGISTRATION_REMOVED:   return "REGISTRATION_REMOVED";	
    case REGISTRATION_EXPIRED:   return "REGISTRATION_EXPIRED";	
    case REASSEMBLY_COMPLETED:   return "REASSEMBLY_COMPLETED";	

    case ROUTE_ADD:              return "ROUTE_ADD";	
    case ROUTE_DEL:              return "ROUTE_DEL";			
        
    default:			return "(invalid event type)";
    }
}

/**
 * Event base class.
 */
class BundleEvent {
public:
    /**
     * The event type code;
     */
    const event_type_t type_;
     
    /**
     * Used for printing
     */
    const char* type_str() { 
        return event_to_str(type_);
    }
    
    /**
     * Need a virtual destructor to make sure all the right bits are
     * cleaned up.
     */
    virtual ~BundleEvent() {}
    
protected:
    /**
     * Constructor (protected since one of the subclasses should
     * always be that which is actually initialized.
     */
    BundleEvent(event_type_t type) : type_(type) {};
    
};

/**
 * Event class for new bundle arrivals.
 */
class BundleReceivedEvent : public BundleEvent {
public:
    BundleReceivedEvent(Bundle* bundle, size_t bytes_received)
        : BundleEvent(BUNDLE_RECEIVED),
          bundleref_(bundle, "BundleReceivedEvent"),
          bytes_received_(bytes_received) {}

    /// The newly arrived bundle
    BundleRef bundleref_;

    /// The total bytes actually received
    size_t bytes_received_;
};

/**
 * Event class for bundle or fragment transmission.
 */
class BundleTransmittedEvent : public BundleEvent {
public:
    BundleTransmittedEvent(Bundle* bundle, BundleConsumer* consumer,
                           size_t bytes_sent, bool acked)
        : BundleEvent(BUNDLE_TRANSMITTED),
          bundleref_(bundle, "BundleTransmittedEvent"),
          consumer_(consumer),
          bytes_sent_(bytes_sent), acked_(acked) {}
    
    /// The transmitted bundle
    BundleRef bundleref_;

    /// The contact or registration where the bundle was sent
    BundleConsumer* consumer_;

    /// Total number of bytes sent
    size_t bytes_sent_;

    /// Indication if the destination acknowledged bundle receipt
    bool acked_;

    /// XXX/demmer should have bytes_acked 
};


/**
 * Event class for contact events
 */
class ContactUpEvent : public BundleEvent {
public:
    ContactUpEvent(Contact* contact)
        : BundleEvent(CONTACT_UP), contact_(contact) {}
    
    /// The contact that is up
    Contact* contact_;
};

class ContactDownEvent : public BundleEvent {
public:
    ContactDownEvent(Contact* contact)
        : BundleEvent(CONTACT_DOWN), contact_(contact) {}
    
    /// The contact that is up
    Contact* contact_;
};

/**
 * Event class for link events
 */
class LinkCreatedEvent : public BundleEvent {
public:
    LinkCreatedEvent(Link* link)
        : BundleEvent(LINK_CREATED), link_(link) {}
    
    Link* link_;
};

class LinkDeletedEvent : public BundleEvent {
public:
    LinkDeletedEvent(Link* link)
        : BundleEvent(LINK_DELETED), link_(link) {}
    
    /// The link that is up
    Link* link_;
};

class LinkAvailableEvent : public BundleEvent {
public:
    LinkAvailableEvent(Link* link)
        : BundleEvent(LINK_AVAILABLE), link_(link) {}
    
    Link* link_;
};

class LinkUnavailableEvent : public BundleEvent {
public:
    LinkUnavailableEvent(Link* link)
        : BundleEvent(LINK_UNAVAILABLE), link_(link) {}
    
    /// The link that is up
    Link* link_;
};

/**
 * Event class for new registration arrivals.
 */
class RegistrationAddedEvent : public BundleEvent {
public:
    RegistrationAddedEvent(Registration* reg)
        : BundleEvent(REGISTRATION_ADDED), registration_(reg) {}

    /// The newly added registration
    Registration* registration_;
};

/**
 * Event class for route add events
 */
class RouteAddEvent : public BundleEvent {
public:
    RouteAddEvent(RouteEntry* entry)
        : BundleEvent(ROUTE_ADD), entry_(entry) {}
    
    /// The route table entry to be added
    RouteEntry* entry_;
};

/**
 * Event class for reassembly completion.
 */
class ReassemblyCompletedEvent : public BundleEvent {
public:
    ReassemblyCompletedEvent(Bundle* bundle, BundleList* fragments)
        : BundleEvent(REASSEMBLY_COMPLETED),
          bundle_(bundle, "ReassemblyCompletedEvent"),
          fragments_("ReassemblyCompletedEvent")
    {
        fragments->move_contents(&fragments_);
    }

    /// The newly reassembled bundle
    BundleRef bundle_;

    /// The list of bundle fragments
    BundleList fragments_;
};

#endif /* _BUNDLE_EVENT_H_ */
