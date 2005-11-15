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

#include <oasys/thread/Notifier.h>
#include <oasys/util/StringBuffer.h>

#include "RegistrationCommand.h"
#include "bundling/BundleDaemon.h"
#include "bundling/BundleEvent.h"
#include "reg/LoggingRegistration.h"
#include "reg/RegistrationTable.h"
#include "reg/TclRegistration.h"

namespace dtn {

RegistrationCommand::RegistrationCommand()
    : TclCommand("registration")
{
    notifier_ = new oasys::Notifier("/notifier/registration_command");
}

const char*
RegistrationCommand::help_string()
{
    return("registration add <logger|tcl> <endpoint> <args...>\n"
           "registration tcl <regid> <cmd> <args...>\n"
           "registration del <regid>\n"
           "registration list");
}

int
RegistrationCommand::exec(int argc, const char** argv, Tcl_Interp* interp)
{
    // need a subcommand
    if (argc < 2) {
        wrong_num_args(argc, argv, 1, 2, INT_MAX);
        return TCL_ERROR;
    }
    const char* op = argv[1];

    if (strcmp(op, "list") == 0 || strcmp(op, "dump") == 0) {
        oasys::StringBuffer buf;
        BundleDaemon::instance()->reg_table()->dump(&buf);
        set_result(buf.c_str());
        return TCL_OK;
        
    } else if (strcmp(op, "add") == 0) {
        // registration add <logger|tcl> <demux> <args...>
        if (argc < 4) {
            wrong_num_args(argc, argv, 2, 4, INT_MAX);
            return TCL_ERROR;
        }

        const char* type = argv[2];
        const char* eid_str = argv[3];
        EndpointIDPattern eid(eid_str);
        
        if (!eid.valid()) {
            resultf("error in registration add %s %s: invalid endpoint id",
                    type, eid_str);
            return TCL_ERROR;
        }

        Registration* reg = NULL;
        if (strcmp(type, "logger") == 0) {
            reg = new LoggingRegistration(eid);
            
        } else if (strcmp(type, "tcl") == 0) {
            reg = new TclRegistration(eid, interp);
            
        } else {
            resultf("error in registration add %s %s: invalid type",
                    type, eid_str);
            return TCL_ERROR;
        }

        ASSERT(reg);

        BundleDaemon::post_and_wait(new RegistrationAddedEvent(reg),
                                    notifier_);
        
        resultf("%d", reg->regid());
        return TCL_OK;
        
    } else if (strcmp(op, "del") == 0) {
        const RegistrationTable* regtable =
            BundleDaemon::instance()->reg_table();

        const char* regid_str = argv[2];
        int regid = atoi(regid_str);

        Registration* reg = regtable->get(regid);
        if (!reg) {
            resultf("no registration exists with id %d", regid);
            return TCL_ERROR;
        }

        BundleDaemon::post_and_wait(new RegistrationRemovedEvent(reg),
                                    notifier_);
        return TCL_OK;

    } else if (strcmp(op, "tcl") == 0) {
        // registration tcl <regid> <cmd> <args...>
        if (argc < 4) {
            wrong_num_args(argc, argv, 2, 5, INT_MAX);
            return TCL_ERROR;
        }

        const char* regid_str = argv[2];
        int regid = atoi(regid_str);

        const RegistrationTable* regtable =
            BundleDaemon::instance()->reg_table();

        TclRegistration* reg = (TclRegistration*)regtable->get(regid);

        if (!reg) {
            resultf("no matching registration for %d", regid);
            return TCL_ERROR;
        }
        
        return reg->exec(argc - 3, &argv[3], interp);
    }

    resultf("invalid registration subcommand '%s'", op);
    return TCL_ERROR;
}

} // namespace dtn
