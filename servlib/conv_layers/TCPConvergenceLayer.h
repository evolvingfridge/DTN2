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
#ifndef _TCP_CONVERGENCE_LAYER_H_
#define _TCP_CONVERGENCE_LAYER_H_

#include <oasys/io/TCPClient.h>
#include <oasys/io/TCPServer.h>

#include "IPConvergenceLayer.h"

namespace dtn {

/**
 * The TCP Convergence Layer.
 *
 * The implementation is fairly straightforward (hopefully). It makes
 * use of three helper classes, Listener, Connection, and Params. The
 * Listener is attached to an interface and, as expected, opens a
 * listening socket on the specified port and waits for new
 * connections. A pointer to the Listener is stored in the CLInfo slot
 * in the interface.
 *
 * The link's CLInfo slot holds a pointer to a Params struct, to keep
 * track of the configured parameters for that link. Each time a new
 * Contact is created (when the link is opened), a Connection object
 * with an associated thread is also created and stored in the CLInfo
 * slot of the Contact.
 *
 * A slightly more complicated situation arises with the
 * reciever_connect option. To implement this feature, intended to
 * allow a TCPCL connection to traverse a firewall or NAT box, the
 * user would configure a special interface with the receiver_connect
 * parameter set. In this case, rather than creating a Listener, the
 * interface would create a new Connection object and initiate contact
 * with the specified peer.
 *
 * To the connection receiver (who will be the data sender), a new
 * OPPORTUNISTIC link appears when the contact is completed. In this
 * case, the Connection object already exists (having been created to
 * accept the connection), so it is handed to the ContactManager to be
 * installed in the new Contact structure.
 */
class TCPConvergenceLayer : public IPConvergenceLayer {
public:
    /**
     * Constructor.
     */
    TCPConvergenceLayer();
    
    /**
     * Register a new interface.
     */
    bool add_interface(Interface* iface, int argc, const char* argv[]);

    /**
     * Remove an interface
     */
    bool del_interface(Interface* iface);

    /**
     * Register a new link.
     */
    bool add_link(Link* link, int argc, const char* argv[]);
    
    /**
     * Remove a link.
     */
    bool del_link(Link* link);

    /**
     * Open the connection to the given contact and prepare for
     * bundles to be transmitted.
     */
    bool open_contact(Contact* contact);
    
    /**
     * Close the connnection to the contact.
     */
    bool close_contact(Contact* contact);

    /**
     * Tunable parameters, defaults configurably for all connections
     * via the 'param set' command'. Per-connection values are also
     * configurable via arguments to the 'link add' command.
     */
    class Params : public CLInfo {
    public:
        bool bundle_ack_enabled_;	///< Are per-bundle acks enabled
        bool receiver_connect_;		///< rcvr-initiated connect (for NAT)
        u_int ack_blocksz_;		///< Bytes to send before ack
        u_int keepalive_interval_;	///< Seconds between keepalive pacekts
        u_int idle_close_time_;		///< Seconds to keep idle connections
        u_int connect_timeout_;		///< Msecs for connection timeout
        u_int rtt_timeout_;		///< Msecs to wait for data
        int test_fragment_size_;	///< Test hook to force reactive frag.
    };

    /**
     * Default parameters.
     */
    static Params defaults_;

protected:
    bool parse_params(Params* params, int argc, const char** argv,
                      const char** invalidp);

    /**
     * Helper class (and thread) that listens on a registered
     * interface for new connections.
     */
    class Listener : public CLInfo, public oasys::TCPServerThread {
    public:
        Listener(Params* params);
        void accepted(int fd, in_addr_t addr, u_int16_t port);

        /**
         * Per-connection parameters for accepted connections.
         */
        TCPConvergenceLayer::Params params_;
    };

    /**
     * Helper class (and thread) that manages an established
     * connection with a peer daemon.
     *
     * Although the same class is used in both cases, a particular
     * Connection is either a receiver or a sender. Note that to deal
     * with NAT, the side which does the active connect is not
     * necessarily the sender.
     */
    class Connection : public CLInfo,
                       public oasys::Thread,
                       public oasys::Logger {
    public:
        /**
         * Constructor for the active connection side of a connection.
         * Note that this may be used both for the actual data sender
         * or for the data receiver side when used with the
         * receiver_connect option.
         */
        Connection(in_addr_t remote_addr,
                   u_int16_t remote_port,
                   bool is_sender,
                   Params* params);
        
        /**
         * Constructor for the passive accept side of a connection.
         */
        Connection(int fd,
                   in_addr_t remote_addr,
                   u_int16_t remote_port,
                   Params* params);

        /**
         * Destructor.
         */
        ~Connection();

        /**
         * Per-connection parameters.
         */
        TCPConvergenceLayer::Params params_;
        
        /**
         * Attach to the given contact. Used on the sender side when
         * the link is activated.
         */
        void set_contact(Contact* contact) { contact_ = contact; }

    protected:
        virtual void run();
        void send_loop();
        void recv_loop();
        void break_contact();
        bool connect();
        bool accept();
        bool send_contact_header();
        bool recv_contact_header(int timeout);
        bool send_bundle(Bundle* bundle, size_t* acked_len);
        bool recv_bundle();
        int  handle_ack(Bundle* bundle, size_t* acked_len);
        bool send_ack(u_int32_t bundle_id, size_t acked_len);
        void note_data_rcvd();

        BundleTuple nexthop_;		///< The next hop we're connected to
        bool is_sender_;		///< Are we the sender side
        Contact* contact_;		///< Contact for sender-side
        oasys::TCPClient* sock_;	///< The socket
        struct timeval data_rcvd_;	///< Timestamp for idle timer
    };
};

} // namespace dtn

#endif /* _TCP_CONVERGENCE_LAYER_H_ */
