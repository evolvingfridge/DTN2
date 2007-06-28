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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <oasys/util/HexDumpBuffer.h>
#include <oasys/util/StringBuffer.h>
#include <oasys/util/OptParser.h>

#include "BundleCommand.h"
#include "CompletionNotifier.h"
#include "bundling/Bundle.h"
#include "bundling/BundleEvent.h"
#include "bundling/BundleDaemon.h"
#include "reg/RegistrationTable.h"
#include "reg/TclRegistration.h"

namespace dtn {

BundleCommand::BundleCommand()
    : TclCommand("bundle") 
{
    add_to_help("inject <src> <dst> <payload> <opt1=val1> .. <optN=valN>",
                "valid options:\n"
                "            custody_xfer\n"
                "            receive_rcpt\n"
                "            custody_rcpt\n"
                "            forward_rcpt\n"
                "            delivery_rcpt\n"
                "            deletion_rcpt\n"
                "            expiration=integer\n"
                "            length=integer\n");
    add_to_help("stats", "get statistics on the bundles");
    add_to_help("daemon_stats", "daemon stats");
    add_to_help("reset_stats", "reset currently maintained statistics");
    add_to_help("list", "list all of the bundles in the system");
    add_to_help("info <id>", "get info on a specific bundle");
    add_to_help("dump <id>", "dump a specific bundle");
    add_to_help("dump_tcl <id>", "dump a bundle as a tcl list");
    add_to_help("dump_ascii <id>", "dump the bundle in ascii");
    add_to_help("expire <id>", "force a specific bundle to expire");
    add_to_help("cancel <id> <link>", "cancel a bundle being sent on a link");
    add_to_help("daemon_idle_shutdown <secs>",
                "shut down the bundle daemon after an idle period");
}

BundleCommand::InjectOpts::InjectOpts()
    : custody_xfer_(false),
      receive_rcpt_(false), 
      custody_rcpt_(false), 
      forward_rcpt_(false), 
      delivery_rcpt_(false), 
      deletion_rcpt_(false), 
      expiration_(60),  // bundle TTL
      length_(0),  // bundle payload length
      replyto_("")
{}
    
bool
BundleCommand::parse_inject_options(InjectOpts* options,
                                    int objc, Tcl_Obj** objv,
                                    const char** invalidp)
{
    // no options specified:
    if (objc < 6) {
        return true;
    }
    
    oasys::OptParser p;

    p.addopt(new oasys::BoolOpt("custody_xfer",  &options->custody_xfer_));
    p.addopt(new oasys::BoolOpt("receive_rcpt",  &options->receive_rcpt_));
    p.addopt(new oasys::BoolOpt("custody_rcpt",  &options->custody_rcpt_));
    p.addopt(new oasys::BoolOpt("forward_rcpt",  &options->forward_rcpt_));
    p.addopt(new oasys::BoolOpt("delivery_rcpt", &options->delivery_rcpt_));
    p.addopt(new oasys::BoolOpt("deletion_rcpt", &options->deletion_rcpt_));
    p.addopt(new oasys::UIntOpt("expiration",    &options->expiration_));
    p.addopt(new oasys::UIntOpt("length",        &options->length_));
    p.addopt(new oasys::StringOpt("replyto",     &options->replyto_));

    for (int i=5; i<objc; i++) {
        int len;
        const char* option_name = Tcl_GetStringFromObj(objv[i], &len);
        if (! p.parse_opt(option_name, len)) {
            *invalidp = option_name;
            return false;
        }
    }
    return true;
}

int
BundleCommand::exec(int objc, Tcl_Obj** objv, Tcl_Interp* interp)
{
    // need a subcommand
    if (objc < 2) {
        wrong_num_args(objc, objv, 1, 2, INT_MAX);
        return TCL_ERROR;
    }

    const char* cmd = Tcl_GetStringFromObj(objv[1], 0);

    if (strcmp(cmd, "inject") == 0) {
        // bundle inject <source> <dest> <payload> <param1<=value1?>?> ... <paramN<=valueN?>?>
        if (objc < 5) {
            wrong_num_args(objc, objv, 2, 5, INT_MAX);
            return TCL_ERROR;
        }
        
        bool eids_valid = true;
        Bundle* b = new Bundle();
        eids_valid &= b->source_.assign(Tcl_GetStringFromObj(objv[2], 0));
        eids_valid &= b->replyto_.assign(Tcl_GetStringFromObj(objv[2], 0));
        b->custodian_.assign(EndpointID::NULL_EID());
        eids_valid &= b->dest_.assign(Tcl_GetStringFromObj(objv[3], 0));
        b->singleton_dest_ = b->dest_.is_singleton();
        
        if (!eids_valid) {
            resultf("bad value for one or more EIDs");
            delete b;
            return TCL_ERROR;
        }
        
        int payload_len;
        u_char* payload_data = Tcl_GetByteArrayFromObj(objv[4], &payload_len);

        // now process any optional parameters:
        InjectOpts options;
        const char* invalid;
        if (!parse_inject_options(&options, objc, objv, &invalid)) {
            resultf("error parsing bundle inject options: invalid option '%s'",
                    invalid);
            delete b;
            return TCL_ERROR;
        }

        b->custody_requested_ = options.custody_xfer_;
        b->receive_rcpt_      = options.receive_rcpt_;
        b->custody_rcpt_      = options.custody_rcpt_;
        b->forward_rcpt_      = options.forward_rcpt_;
        b->delivery_rcpt_     = options.delivery_rcpt_;
        b->deletion_rcpt_     = options.deletion_rcpt_;
        b->expiration_        = options.expiration_;
        
        // Bundles with a null source EID are not allowed to request reports or
        // custody transfer, and must not be fragmented.
        if (b->source_ == EndpointID::NULL_EID()) {
            if ( b->custody_requested_ ||
                 b->receipt_requested() ||
                 b->app_acked_rcpt_) {
                log_err("bundle with null source EID cannot request reports or "
                        "custody transfer");
                delete b;
                return TCL_ERROR;
            }
            
            b->do_not_fragment_ = true;
        }
        
        else {
            // The bundle's source EID must be either dtn:none or an EID 
            // registered at this node.
            const RegistrationTable* reg_table = 
                    BundleDaemon::instance()->reg_table();
            std::string base_reg_str = b->source_.uri().scheme() + "://" + 
                    b->source_.uri().host();
            
            if (!reg_table->get(EndpointIDPattern(base_reg_str)) &&
                !reg_table->get(EndpointIDPattern(b->source_))) { 
                log_err("this node is not a member of the bundle's source EID (%s)",
                        b->source_.str().c_str());
                delete b;
                return TCL_ERROR;
            }
        }

        if (options.length_ != 0) {
            // explicit length but some of the data may just be left
            // as garbage. 
            b->payload_.set_length(options.length_);
            if (payload_len != 0) {
                b->payload_.write_data(payload_data, payload_len, 0);
            }

            // make sure to write a byte at the end of the payload to
            // properly fool the BundlePayload into thinking that we
            // actually got all the data
            u_char byte = 0;
            b->payload_.write_data(&byte, options.length_ - 1, 1);
            
            payload_len = options.length_;
        } else {
            // use the object length
            b->payload_.set_data(payload_data, payload_len);
        }
        
        if (options.replyto_ != "") {
            b->replyto_.assign(options.replyto_.c_str());
        }

        oasys::StringBuffer error;
        if (!b->validate(&error)) {
            resultf("bundle validation failed: %s", error.data());
            return TCL_ERROR;
        }
        
        log_debug("inject %d byte bundle %s->%s", payload_len,
                  b->source_.c_str(), b->dest_.c_str());

        BundleDaemon::post(new BundleReceivedEvent(b, EVENTSRC_APP));

        // return the creation timestamp (can use with source EID to
        // create a globally unique bundle identifier
        resultf("%u.%u", b->creation_ts_.seconds_, b->creation_ts_.seqno_);
        return TCL_OK;
        
    } else if (!strcmp(cmd, "stats")) {
        oasys::StringBuffer buf("Bundle Statistics: ");
        BundleDaemon::instance()->get_bundle_stats(&buf);
        set_result(buf.c_str());
        return TCL_OK;

    } else if (!strcmp(cmd, "daemon_stats")) {
        oasys::StringBuffer buf("Bundle Daemon Statistics: ");
        BundleDaemon::instance()->get_daemon_stats(&buf);
        set_result(buf.c_str());
        return TCL_OK;
    } else if (!strcmp(cmd, "daemon_status")) {
        BundleDaemon::post_and_wait(new StatusRequest(),
                                    CompletionNotifier::notifier());
        set_result("DTN daemon ok");
        return TCL_OK;
    } else if (!strcmp(cmd, "reset_stats")) {
        BundleDaemon::instance()->reset_stats();
        return TCL_OK;
        
    } else if (!strcmp(cmd, "list")) {
        Bundle* b;
        BundleList::const_iterator iter;
        oasys::StringBuffer buf;
        BundleList* pending =
            BundleDaemon::instance()->pending_bundles();
        
        oasys::ScopeLock l(pending->lock(), "BundleCommand::exec");
        buf.appendf("Currently Pending Bundles (%zu): \n", pending->size());
    
        for (iter = pending->begin(); iter != pending->end(); ++iter) {
            b = *iter;
            buf.appendf("\t%-3d: %s -> %s length %zu\n",
                        b->bundleid_,
                        b->source_.c_str(),
                        b->dest_.c_str(),
                        b->payload_.length());
        }
        
        set_result(buf.c_str());
        
        return TCL_OK;
        
    } else if (!strcmp(cmd, "info") ||
               !strcmp(cmd, "dump") ||
               !strcmp(cmd, "dump_tcl") ||
               !strcmp(cmd, "dump_ascii") ||
               !strcmp(cmd, "expire"))
    {
        // bundle [info|dump|dump_ascii|expire] <id>
        if (objc != 3) {
            wrong_num_args(objc, objv, 2, 3, 3);
            return TCL_ERROR;
        }

        int bundleid;
        if (Tcl_GetIntFromObj(interp, objv[2], &bundleid) != TCL_OK) {
            resultf("invalid bundle id %s",
                    Tcl_GetStringFromObj(objv[2], 0));
            return TCL_ERROR;
        }

        BundleList* pending =
            BundleDaemon::instance()->pending_bundles();
        
        BundleRef bundle = pending->find(bundleid);

        if (bundle == NULL) {
            resultf("no bundle with id %d", bundleid);
            return TCL_ERROR;
        }

        if (strcmp(cmd, "info") == 0) {
            oasys::StringBuffer buf;
            bundle->format_verbose(&buf);
            set_result(buf.c_str());

        } else if (strcmp(cmd, "dump_tcl") == 0) {
            Tcl_Obj* result = NULL;
            int ok =
                TclRegistration::parse_bundle_data(interp, bundle, &result);
            
            set_objresult(result);
            return ok;
            
        } else if (strcmp(cmd, "dump_ascii") == 0) {
            size_t len = bundle->payload_.length();
            oasys::StringBuffer buf(len);
            const u_char* bp = 
                    bundle->payload_.read_data(0, len, (u_char*)buf.data());
            
            buf.append((const char*)bp, len);            
            set_result(buf.c_str());
            
        } else if (strcmp(cmd, "dump") == 0) {
            size_t len = bundle->payload_.length();
            oasys::HexDumpBuffer buf(len);
            
            bundle->payload_.read_data(0, len, (u_char*)buf.tail_buf(len));
            buf.incr_len(len);
            
            set_result(buf.hexify().c_str());
            
        } else if (strcmp(cmd, "expire") == 0) {
            BundleDaemon::instance()->post_at_head(
                new BundleExpiredEvent(bundle.object()));
            return TCL_OK;
        }
        
        return TCL_OK;

    } else if (!strcmp(cmd, "cancel")) {
        // bundle cancel <id> <link>
        if (objc != 4) {
            wrong_num_args(objc, objv, 2, 4, 4);
            return TCL_ERROR;
        }

        int bundleid;
        if (Tcl_GetIntFromObj(interp, objv[2], &bundleid) != TCL_OK) {
            resultf("invalid bundle id %s",
                    Tcl_GetStringFromObj(objv[2], 0));
            return TCL_ERROR;
        }

        const char* name = Tcl_GetStringFromObj(objv[3], 0);

        BundleRef br 
            = BundleDaemon::instance()->pending_bundles()->find(bundleid);

        BundleDaemon::instance()->post_at_head(
                new BundleCancelRequest(br, name));
        
        return TCL_OK;
    } else if (!strcmp(cmd, "daemon_idle_shutdown")) {
        oasys::StringBuffer buf("Bundle Daemon Statistics: ");
        
        if (objc != 3) {
            wrong_num_args(objc, objv, 2, 3, 3);
            return TCL_ERROR;
        }

        int interval;
        if (Tcl_GetIntFromObj(interp, objv[2], &interval) != TCL_OK) {
            resultf("invalid interval %s",
                    Tcl_GetStringFromObj(objv[2], 0));
            return TCL_ERROR;
        }

        BundleDaemon::instance()->init_idle_shutdown(interval);
        return TCL_OK;
        
    } else {
        resultf("unknown bundle subcommand %s", cmd);
        return TCL_ERROR;
    }
}


} // namespace dtn
