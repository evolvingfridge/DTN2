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

#include "SimContact.h"
#include "Simulator.h"

namespace dtnsim {


SimContact::SimContact (int id, Node* src, Node* dst, 
double bw, double latency, bool isup, int pup, int pdown) 
    : Logger ("/sim/simcontact") 
{
    id_ = id;
    src_ = src;
    dst_ = dst;
    bw_ = bw;
    latency_ = latency;
    up_ = pup; // length of up period
    down_ = pdown; // length of down period
    ASSERT(bw > 0 && latency >= 0) ;
    
    log_info("C[%d]: CONTACT CREATED: N[%d]->N[%d]\n",
             id_,src_->id(),dst_->id());
    
    chewing_event_ = NULL;
    future_updown_event_ = NULL;
    // start state
    if (isup) {
        open_contact(false);
    } else  {
        close_contact(false);
    }
}


// This defines behavior of starting of eating of a message
void 
SimContact::chew_message(Message* msg) 
{
    log_info("C[%d]: CH_ST id:%d src:%d size:%f",
        id(), msg->id(), msg->src(),msg->size());
    
    ASSERT(state_ == OPEN);
    state_ = BUSY;
    double tr = msg->size()/bw_;
    ASSERT(tr >0);
    double tmp = Simulator::time() + tr ; 
    Event_chew_fin* e2 =
        new Event_chew_fin(tmp,this,msg,Simulator::time());
    chewing_event_ = e2;
    log_debug("ChSt-ChEvent[%d]: event:%p",id(), e2);
    Simulator::post(e2);
}


bool
SimContact::is_open() 
{
    return state_ == OPEN ;
}



void 
SimContact::chewing_complete(double size, Message* msg) 
{
    log_info("C[%d]: CH_CMP id:%d src:%d size:%2f trans:%2f", 
             id(),msg->id(),msg->src(),msg->size(),size);
    if (size > msg->size()) {
        log_debug(" transmit size exceeding message (%d) size (%2f > %2f) "
                  ,msg->id(),size,msg->size());
        size = msg->size();
    }
    
    if (state_ == CLOSE)
        log_err("C[%d]: closed when chewing complete",id());
    
    if (size != 0) {
        Event_message_received* e = 
            new Event_message_received(Simulator::time()+latency_,dst_,size,this,msg);
        Simulator::post(e);
    }
    src_->chewing_complete(this,size,msg);
}


void 
remove_event(Event* e) 
{
    if (e != NULL)
        Simulator::remove_event(e);
}


void
SimContact::open_contact(bool forever) 
{
    remove_event(future_updown_event_);
    future_updown_event_ = NULL;
    
    // if it is a regular scheduled open, schedule a future down event
    if (!forever) {
        // schedule a link down event
        double next = up_;
        Event_contact_down* e = new Event_contact_down(Simulator::time()+next,this);
        Simulator::post(e);
        future_updown_event_ = e;
    }
    
    state_ = OPEN;
    // send message to src, telling about that the contact is available
    src_->open_contact(this);
}



void 
SimContact::close_contact(bool forever) 
{

    remove_event(future_updown_event_);
    future_updown_event_ = NULL;
    if (!forever) {
        // schedule a link up event
        double next = down_;
        Event_contact_up* e = new Event_contact_up(Simulator::time()+next,this);
        Simulator::post(e);
        future_updown_event_ = e;
    }
    
    
    if (state_ == BUSY) {
        log_info("C[%d]: state busy while closing",
                 id());
        // fragmentation has to occur
        ASSERT(chewing_event_ != NULL);
        // schedule partial chewing complete
        double transmit_time = Simulator::time() - chewing_event_->chew_starttime_;
        double size_transmit = (bw_*transmit_time);
        
        if (!SimContact::ALLOW_FRAGMENTATION) {
            size_transmit = 0;
        } else if (SimContact::DISALLOW_FRACTIONAL_TRANSFERS) {
            size_transmit = (int)(size_transmit);
        }
        chewing_complete(size_transmit,chewing_event_->msg_);
        remove_event(chewing_event_);
        chewing_event_ = NULL;
        
    }
    // tell the source that this contact is closed
    src_->close_contact(this);
    state_ = CLOSE;
}


void 
SimContact::process(Event* e) 
{
    switch (e->type()) {
    case CONTACT_UP : {
        log_info("C[%d]: N[%d]->N[%d] UP NOW",id(),src_->id(),dst_->id());
        open_contact(((Event_contact_up* )e)->forever_);
        break;
    }
    case CONTACT_DOWN : {
        log_info("C[%d]: N[%d]->N[%d] DOWN NOW",id(),src_->id(),dst_->id());
        close_contact(((Event_contact_down* )e)->forever_);
        break;
    }
    case CONTACT_CHEWING_FINISHED : {
        Event_chew_fin* e1 = (Event_chew_fin* )e;
        log_debug(
            "CChFin[%d] event:%p bundleid:%d chewing_event:%p start at:%f",
            id(),e,e1->msg_->id(),chewing_event_,e1->chew_starttime_);
        if (state_ == CLOSE)  {
            ASSERT(false);
            return;
        }
        state_ = OPEN;
        log_debug("chewing_event NULL:%p",chewing_event_);
	    
        //we have to make sure that this event is set to NULL
        //before we call chewing_complete
        chewing_event_ = NULL;
        chewing_complete( e1->msg_->size(),e1->msg_);
        //src_->open_contact(this);
        break;
    } 
    default:
        PANIC("undefined event \n");
    }
}






} // namespace dtnsim
