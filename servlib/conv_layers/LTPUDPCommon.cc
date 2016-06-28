/*
 *    Copyright 2015 United States Government as represented by NASA
 *       Marshall Space Flight Center. All Rights Reserved.
 *
 *    Released under the NASA Open Source Software Agreement version 1.3;
 *    You may obtain a copy of the Agreement at:
 * 
 *        http://ti.arc.nasa.gov/opensource/nosa/
 * 
 *    The subject software is provided "AS IS" WITHOUT ANY WARRANTY of any kind,
 *    either expressed, implied or statutory and this agreement does not,
 *    in any manner, constitute an endorsement by government agency of any
 *    results, designs or products rsulting from use of the subject software.
 *    See the Agreement for the specific language governing permissions and
 *    limitations.
 */

#ifdef HAVE_CONFIG_H
#    include <dtn-config.h>
#endif

#ifdef LTPUDP_ENABLED


#include "LTPUDPCommon.h"
#include "bundling/BundleList.h"
#include <oasys/util/HexDumpBuffer.h>

#include <sys/time.h>
#include <errno.h>

#ifdef LTPUDP_AUTH_ENABLED
#    ifndef BSP_ENABLED
#        error "LTPUDP Authentication requires BSP_ENABLED: configure including --with-bsp"
#    endif
#    include <security/KeyDB.h>
#    include <naming/EndpointID.h>
#    include <openssl/sha.h>
      static char  HMAC_NULL_CIPHER_KEY[] = {0xc3,0x7b,0x7e,0x64,0x92,0x58,0x43,0x40,
                                             0xbe,0xd1,0x22,0x07,0x80,0x89,0x41,0x15,
                                             0x50,0x68,0xf7,0x38};
#endif


#define CHECK_DECODE_STATUS  \
if(decode_status > 0) { \
    current_byte += decode_status;\
} else { \
    is_valid_=false;\
    return -1; \
}

namespace dtn
{
//
//   LTPUDPSession ......................
//
//----------------------------------------------------------------------    
LTPUDPSession::LTPUDPSession(u_int64_t engine_id, u_int64_t session_id, LTPUDPCallbackIF * parent, int parent_type) :
        oasys::Logger("LTPUDPSession","/dtn/cl/ltpudp/session/%p", this)
{
     engine_id_            = engine_id;
     session_id_           = session_id;
     parent_               = parent;
     parent_type_          = parent_type;

     char key[50];
     sprintf(key, "%20.20"PRIu64":%20.20"PRIu64, engine_id, session_id);
     session_key_          = key;

     init();
}

//----------------------------------------------------------------------    
LTPUDPSession::~LTPUDPSession()
{
     Cancel_Inactivity_Timer();
     RemoveSegments();
     delete bundle_list_;
}

//----------------------------------------------------------------------    
void
LTPUDPSession::init()
{
     red_complete_           = false;
     green_complete_         = false;
     red_processed_          = false;
     green_processed_        = false;
     expected_red_bytes_     = 0;
     expected_green_bytes_   = 0;
     session_size_           = 0;
     report_serial_number_   = 0;
     cancel_segment_         = NULL;
     inactivity_timer_       = NULL;
     last_packet_time_       = 0;
     checkpoint_id_          = LTPUDP_14BIT_RAND; // 1 through (2**14)-1=0x3FFF
     bundle_list_            = new BundleList(session_key_);
     session_status_         = LTPUDP_SESSION_STATUS_INITIALIZED;
     session_state_          = LTPUDP_SESSION_STATE_CREATED;
     reports_sent_           = 0;
     inbound_cipher_suite_   = -1;
     inbound_cipher_key_id_  = -1;
     inbound_cipher_engine_  = "";
     outbound_cipher_suite_  = -1;
     outbound_cipher_key_id_ = -1;
     outbound_cipher_engine_ = "";
     is_security_enabled_    = false;
}

//----------------------------------------------------------------------
void 
LTPUDPSession::Set_Inbound_Cipher_Suite(int new_val)
{
    is_security_enabled_ = (new_val != -1) ? true : false;
    inbound_cipher_suite_ = new_val;
}
//----------------------------------------------------------------------
void
LTPUDPSession::Set_Inbound_Cipher_Key_Id(int new_key)
{
    inbound_cipher_key_id_ = new_key;
}
//----------------------------------------------------------------------
void 
LTPUDPSession::Set_Inbound_Cipher_Engine(string new_engine)
{
    inbound_cipher_engine_ = new_engine;
}
//----------------------------------------------------------------------
void 
LTPUDPSession::Set_Outbound_Cipher_Suite(int new_val)
{
    is_security_enabled_ = (new_val != -1) ? true : false;
    outbound_cipher_suite_ = new_val;
}
//----------------------------------------------------------------------
void
LTPUDPSession::Set_Outbound_Cipher_Key_Id(int new_key)
{
    outbound_cipher_key_id_ = new_key;
}
//----------------------------------------------------------------------
void 
LTPUDPSession::Set_Outbound_Cipher_Engine(string new_engine)
{
    outbound_cipher_engine_ = new_engine;
}
//----------------------------------------------------------------------    
void 
LTPUDPSession::SetAggTime()
{
    agg_start_time_.get_time();
}

//----------------------------------------------------------------------    
bool 
LTPUDPSession::TimeToSendIt(uint32_t milliseconds)
{
    return agg_start_time_.elapsed_ms() >= milliseconds;
}


//----------------------------------------------------------------------    
void 
LTPUDPSession::RemoveSegments()
{
     //triggers segfault on some systems when called from the destructor:
     //log_debug("RemoveSegments: Deleting red: %zu green: %zu report: %zu cancel: %d  bundles: %zu",
     //          red_segments_.size(), green_segments_.size(), 
     //          report_segments_.size(), (NULL == cancel_segment_ ? 0 : 1),
     //          bundle_list_->size());

     // clear the red segments
     DS_MAP::iterator seg_iter = red_segments_.begin();
     while(seg_iter != red_segments_.end()) {
         LTPUDPSegment * segptr = seg_iter->second; 
         segptr->Cancel_Retransmission_Timer();
         delete segptr;
         red_segments_.erase(seg_iter);
         seg_iter = red_segments_.begin();
     }

     // clear the green segments
     seg_iter = green_segments_.begin();
     while(seg_iter != green_segments_.end()) {
         LTPUDPSegment * segptr = seg_iter->second; 
         segptr->Cancel_Retransmission_Timer();
         delete segptr;
         green_segments_.erase(seg_iter);
         seg_iter = green_segments_.begin();
     }

     // clear the report map
     RS_MAP::iterator rs_iter = report_segments_.begin();
     while(rs_iter != report_segments_.end()) {
         LTPUDPReportSegment * rptsegptr = rs_iter->second; 
         rptsegptr->Cancel_Retransmission_Timer();
         delete rptsegptr;
         report_segments_.erase(rs_iter);
         rs_iter = report_segments_.begin();
     }

     // clear the cancel segment
     if(cancel_segment_ != (LTPUDPCancelSegment *) NULL)
     {
         cancel_segment_->Cancel_Retransmission_Timer();
         delete cancel_segment_;
         cancel_segment_ = (LTPUDPCancelSegment *) NULL;
     }

     bundle_list_->clear();
}

//----------------------------------------------------------------------    
void
LTPUDPSession::Set_Cancel_Segment(LTPUDPCancelSegment* new_segment)
{
     if(cancel_segment_ != (LTPUDPCancelSegment *) NULL)
     {
         delete cancel_segment_;
     }
     cancel_segment_ = new_segment;
}

//----------------------------------------------------------------------    
void 
LTPUDPSession::Set_Checkpoint_ID(u_int64_t in_checkpoint)
{
     checkpoint_id_  = in_checkpoint;
}

//----------------------------------------------------------------------    
u_int64_t 
LTPUDPSession::Increment_Checkpoint_ID()
{
     checkpoint_id_++;
     return checkpoint_id_;
}

//----------------------------------------------------------------------    
u_int64_t 
LTPUDPSession::Increment_Report_Serial_Number()
{
     report_serial_number_++;
     //log_debug("After Increment Session->Report_Serial_Number = %"PRIu64,report_serial_number_);
     return report_serial_number_;
}

//----------------------------------------------------------------------
void
LTPUDPSession::AddToBundleList(BundleRef bundle, size_t bundle_size)
{
     session_size_ += bundle_size;
     bundle_list_->push_back(bundle);
}

//----------------------------------------------------------------------    
void 
LTPUDPSession::Reset_Inact_Timer()
{
     log_debug("Reset_Inact_Timer: Session: %s  - clearing inactivity timer_: %p",
               session_key_.c_str(), inactivity_timer_);

     inactivity_timer_ = 0;
}

//----------------------------------------------------------------------    
void 
LTPUDPSession::Set_Session_State(int new_session_state)
{
     session_state_  = new_session_state;

     log_debug("Set_Session_State: Session: %s set to %s",
               session_key_.c_str(), Get_Session_State());
}

//----------------------------------------------------------------------    
void 
LTPUDPSession::Set_Report_Serial_Number(u_int64_t in_report_serial)
{ 
     report_serial_number_  = in_report_serial;
}

//----------------------------------------------------------------------    
void 
LTPUDPSession::Set_EOB(LTPUDPDataSegment * sop)
{
     if (sop->IsRed()) {
         expected_red_bytes_ = sop->Stop_Byte()+1;
         log_debug("Set_EOB: Session: %s - Red bytes = %d",
                   session_key_.c_str(), expected_red_bytes_);
         red_complete_          = true;
         sop->Set_Endofblock();
     } else if (sop->IsGreen()) {
         expected_green_bytes_ = sop->Stop_Byte()+1;
         log_debug("Set_EOB: Session: %s - Green bytes = %d",
                   session_key_.c_str(), expected_green_bytes_);
         green_complete_          = true;
         sop->Set_Endofblock();
     } else {
         log_err("Set_EOB: Session: %s - No Red or Green Data?",
                 session_key_.c_str());
     }
}

//----------------------------------------------------------------------    
time_t 
LTPUDPSession::Last_Packet_Time()
{
    return last_packet_time_;
}

//----------------------------------------------------------------------    
void
LTPUDPSession::Set_Cleanup()
{
    last_packet_time_ = 0;
    session_status_   = LTPUDP_SESSION_STATUS_CLEANUP;
}

//----------------------------------------------------------------------    
int 
LTPUDPSession::RemoveSegment(DS_MAP * segments, int reception_offset, int reception_length)
{
    log_debug("RemoveSegment: Session: %s - process report claim - offset: %d length: %d", 
              session_key_.c_str(), reception_offset, reception_length);

    // return 1 to indicate that a checkpoint segment was claimed else zero
    // XXX/dz ION 3.3.0 limits report segments to 20 claims so it can take multiple
    //        RS to complete a report and we want to let the caller know if this
    //        claim completes the report
    int checkpoint_claimed = 0;
    if (segments->size() > 0) {
        DS_MAP::iterator seg_iter;
        DS_MAP::iterator del_seg_iter;

        for(seg_iter=segments->begin(); seg_iter!=segments->end(); ) 
        {
            string remove_key = seg_iter->first;

            LTPUDPDataSegment * this_segment = seg_iter->second;
 
            if((int) this_segment->Offset() == reception_offset && (int) this_segment->Payload_Length() == reception_length)
            {
                checkpoint_claimed = this_segment->IsCheckpoint() ? 1 : checkpoint_claimed;
                this_segment->Cancel_Retransmission_Timer(); // just in case one is still out there.
                log_debug("RemoveSegment: Session: %s - claim for single segment: %s checkpoint: %d", 
                          session_key_.c_str(), remove_key.c_str(), checkpoint_claimed);
                delete this_segment;
                segments->erase(seg_iter);
                break;
            }
            else if((int) this_segment->Offset() >= reception_offset && 
                    (int) this_segment->Offset()+(int) this_segment->Payload_Length()-1 < reception_offset+reception_length)
            {
                checkpoint_claimed = this_segment->IsCheckpoint() ? 1 : checkpoint_claimed;
                this_segment->Cancel_Retransmission_Timer(); // just in case one is still out there.
                log_debug("RemoveSegment: Session: %s - claim for multiple segments: %s checkpoint: %d", 
                          session_key_.c_str(), remove_key.c_str(), checkpoint_claimed);
                delete this_segment;
                del_seg_iter = seg_iter;
                seg_iter++; // increment before we remove!
                segments->erase(del_seg_iter);
            } else {
                seg_iter++; // prevent endless testing
            }
        }
    }

    return checkpoint_claimed;
}

//----------------------------------------------------------------------    
int 
LTPUDPSession::AddSegment(LTPUDPDataSegment* sop, bool check_for_overlap)
{
    int return_status = -1;

    DS_MAP::iterator seg_iter;

    last_packet_time_ = parent_->AOS_Counter();

    switch(sop->Red_Green()) {

        case RED_SEGMENT:

          if (!check_for_overlap || CheckAndTrim(sop)) { // make sure there are no collisions...
              log_debug("AddSegment: Session: %s - red key: %s",
                        session_key_.c_str(), sop->Key().c_str());

              if(expected_red_bytes_ < (sop->Stop_Byte()+1))expected_red_bytes_ = (sop->Stop_Byte()+1);
              if ((int) sop->Cipher_Suite() != -1) {
                  log_debug("Adding Segment with Cipher_Suite set to %d\n",sop->Cipher_Suite());
              }
              Red_Segments().insert(std::pair<std::string, LTPUDPDataSegment*>(sop->Key(), sop));
              return_status = 0;
          }
          break;

        case GREEN_SEGMENT:

          if (CheckAndTrim(sop)) { // make sure there are no collisions...
              log_debug("AddSegment: Session: %s - green key: %s",
                        session_key_.c_str(), sop->Key().c_str());

              if ((int) sop->Cipher_Suite() != -1) {
                  log_debug("Adding Segment with Cipher_Suite set to %d\n",sop->Cipher_Suite());
              }
              if(expected_green_bytes_ < (sop->Stop_Byte()+1))expected_green_bytes_ = (sop->Stop_Byte()+1);
              Green_Segments().insert(std::pair<std::string, LTPUDPDataSegment*>(sop->Key(), sop));
              return_status = 0;
          }
          break;

        default:

          // no segment match
          log_debug("AddSegment: Session: %s - Invalid Red/Green indication",
                    session_key_.c_str());

          break;

    }
    return return_status;
}

//----------------------------------------------------------------------    
bool 
LTPUDPSession::CheckAndTrim(LTPUDPDataSegment * sop)
{
    DS_MAP::iterator ltpudp_segment_iterator_;
 
    bool return_stats = false;  // don't insert this segment    
    bool checking     = true; 
    int lookup_status = UNKNOWN_SEGMENT;

    switch(sop->Red_Green())
    {
        case RED_SEGMENT: 

          ltpudp_segment_iterator_ = Red_Segments().find(sop->Key()); // if we find an exact match ignore this one
          if (ltpudp_segment_iterator_ == Red_Segments().end()) {  // we didn't find one...
              while(checking)
              {
                  lookup_status = Check_Overlap(&Red_Segments(), sop); // keep looking until we have pruned the data
                  if (lookup_status == LOOKUP_COMPLETE)
                  {
                      checking = false;
                  }
                  if (lookup_status == DUPLICATE_SEGMENT)
                  {
                      break;
                  }
                  // if ADJUSTED simply keep looking...
              }
              if (lookup_status == LOOKUP_COMPLETE)return_stats = true; // we checked the list...
          }
          break;

        case GREEN_SEGMENT:

          ltpudp_segment_iterator_ = Green_Segments().find(sop->Key());   // if we find an exact match ignore this one
          if (ltpudp_segment_iterator_ == Green_Segments().end()) {  // we didn't find one...
              while(checking)
              { 
                  lookup_status =Check_Overlap(&Green_Segments(), sop); // keep looking until we have pruned the data
                  if(lookup_status == LOOKUP_COMPLETE)
                  {
                      checking = false;
                  }
                  if(lookup_status == DUPLICATE_SEGMENT)
                  {
                      break;
                  }
                  // if ADJUSTED simply keep looking...
              }
              if(lookup_status == LOOKUP_COMPLETE)return_stats = true; // we checked the list...
          }
          break;
    }
    return return_stats;
}  

//----------------------------------------------------------------------    
int 
LTPUDPSession::Check_Overlap(DS_MAP * segments, LTPUDPDataSegment * sop)
{
    int adjust        = 0;
    int return_status = UNKNOWN_SEGMENT;

    if(segments->empty())return LOOKUP_COMPLETE;

    for(DS_MAP::reverse_iterator seg_iter=segments->rbegin(); seg_iter!=segments->rend(); ++seg_iter) {

        bool left_overlap = false;
        bool right_overlap = false;

        string key = seg_iter->first;

        LTPUDPDataSegment * this_segment = seg_iter->second;

        if (sop->Start_Byte() > this_segment->Stop_Byte()) {
            return LOOKUP_COMPLETE;
        }

        if (sop->Start_Byte() <= this_segment->Stop_Byte() &&
            sop->Start_Byte() >= this_segment->Start_Byte()) {
            // left is within
            left_overlap = true;
        }

        if (sop->Stop_Byte() <= this_segment->Stop_Byte() &&
            sop->Stop_Byte() >= this_segment->Start_Byte()) {
            // right is within
            right_overlap = true;
        }

        if (left_overlap && right_overlap)
        {
            return DUPLICATE_SEGMENT;  // we already have this data
        }

        if (sop->Start_Byte() <= this_segment->Start_Byte() &&
            sop->Stop_Byte() >= this_segment->Stop_Byte()) { // toss out the smaller segment 
            segments->erase(this_segment->Key());
            delete this_segment;
            return DELETED_SEGMENT;
        }

        if(left_overlap) 
        {
            //log_debug("Overlap: left_overlap");
            adjust = this_segment->Stop_Byte()-sop->Offset();
            memmove(sop->Payload()->buf(),sop->Payload()->buf()+adjust,sop->Payload_Length()-adjust); // move the data!!!
            sop->Set_Offset(sop->Offset() + adjust);
            sop->Set_Length(sop->Payload_Length() - adjust);
            sop->Set_Start_Byte(sop->Offset());
            sop->Set_Stop_Byte(sop->Offset()+sop->Payload_Length()-1);
            sop->Set_Key();
            return_status    = ADJUSTED_SEGMENT; 
        }

        if(right_overlap) 
        {
            //log_debug("Overlap: right_overlap");
            adjust          = (this_segment->Start_Byte()-1)-sop->Start_Byte();
            // truncate the length  don't bother removing the data since it won't be used
            sop->Set_Length(sop->Payload_Length()-adjust); 
            sop->Set_Stop_Byte(sop->Start_Byte()+(sop->Payload_Length()-1));
            sop->Set_Key();
            return_status   = ADJUSTED_SEGMENT; 
        }

        if(return_status != UNKNOWN_SEGMENT)break;
    }

    // fell through not finding any conflicts
    if(return_status == UNKNOWN_SEGMENT)return_status = LOOKUP_COMPLETE;
    return return_status;
}

//----------------------------------------------------------------------    
int 
LTPUDPSession::IsRedFull() 
{
    if(!red_complete_ || red_processed_)return 0;  // we haven't seen the EOB packet or we already sent this
    
    int total_bytes = 0;
    for(DS_MAP::iterator seg_iter_=Red_Segments().begin(); seg_iter_!=Red_Segments().end(); ++seg_iter_) {

         LTPUDPDataSegment * this_segment = seg_iter_->second;
         total_bytes                     += this_segment->Payload_Length(); 
    }        

    if(total_bytes == expected_red_bytes_)return total_bytes;
    return 0; // we aren't complete yet.
}

//----------------------------------------------------------------------    
int 
LTPUDPSession::IsGreenFull() 
{
    if(!green_complete_ || green_processed_)return false; // we haven't seen the EOB packet or we've already sent this.
    int total_bytes = 0;
    for (DS_MAP::iterator seg_iter_=Green_Segments().begin(); seg_iter_!=Green_Segments().end(); ++seg_iter_) {

         LTPUDPDataSegment * this_segment = seg_iter_->second;
         total_bytes                     += this_segment->Payload_Length(); 
    }        

    if (total_bytes == expected_green_bytes_)return total_bytes;
    return 0; // we aren't complete yet.
}

//----------------------------------------------------------------------    
u_char * 
LTPUDPSession::GetAllRedData()
{
    int bytes_loaded = 0;

    u_char * buffer = (u_char *) malloc(expected_red_bytes_); 

    if(buffer != (u_char *) NULL) {
        for(DS_MAP::iterator seg_iter_=Red_Segments().begin(); seg_iter_!=Red_Segments().end(); ++seg_iter_) {
            LTPUDPDataSegment * this_segment = seg_iter_->second;
            memcpy(&buffer[this_segment->Offset()],this_segment->Payload()->buf(),this_segment->Payload_Length());
            //log_debug("Loading at offset %d (%d) bytes",this_segment->Offset(),this_segment->Payload_Length());
            bytes_loaded += this_segment->Payload_Length();
        }        
        ASSERT(bytes_loaded == expected_red_bytes_);
        red_processed_ = true;
    }       

    log_debug("GetAllRedData: Session: %s - returning %d byte buffer",
              session_key_.c_str(), bytes_loaded);

    return buffer;
}

//----------------------------------------------------------------------    
u_char * 
LTPUDPSession::GetAllGreenData()
{
    int bytes_loaded = 0;
    u_char * buffer = (u_char *) malloc(expected_green_bytes_); 
    if(buffer != (u_char *) NULL) {
        for(DS_MAP::iterator seg_iter_=Green_Segments().begin(); seg_iter_!=Green_Segments().end(); ++seg_iter_) {
            LTPUDPDataSegment * this_segment = seg_iter_->second;
            memcpy(&buffer[this_segment->Offset()],this_segment->Payload()->buf(),this_segment->Payload_Length());
            bytes_loaded += this_segment->Payload_Length();
        }        
        ASSERT(bytes_loaded == expected_green_bytes_);
        green_processed_ = true;
    }

    log_debug("GetAllRedData: Session: %s - returning %d byte buffer",
              session_key_.c_str(), bytes_loaded);

    return buffer;
}

//----------------------------------------------------------------------    
void
LTPUDPSession::Start_Inactivity_Timer(time_t time_left)
{
    // initialize the inactivity timer
    inactivity_timer_ = new InactivityTimer(parent_, session_key_,  parent_type_);
    inactivity_timer_->SetSeconds(time_left);
    inactivity_timer_->schedule_in(time_left*1000); // it needs milliseconds

    log_debug("Start_Inactivity_Timer: Session: %s  seconds: %d",
              session_key_.c_str(), (int) time_left);
}

//----------------------------------------------------------------------    
void
LTPUDPSession::Cancel_Inactivity_Timer()
{
    if(inactivity_timer_ == 0)return;
    if(inactivity_timer_ != 0)
    {
        if(inactivity_timer_->pending() || !inactivity_timer_->cancelled())
        {
            bool cancelled = inactivity_timer_->cancel();
            if(cancelled) {
                log_debug("Cancel_Inactivity_Timer: Session: %s - Timer cancelled",
                          session_key_.c_str());
            } else {
                log_debug("Cancel_Inactivity_Timer: Session: %s - Timer not cancelled",
                          session_key_.c_str());
            }
        }
        inactivity_timer_=0;
    }
}
//----------------------------------------------------------------------    
void
LTPUDPSession::InactivityTimer::timeout(
        const struct timeval &now)
{
    (void)now;
    if (first_call_) {
        // first call is performed in the oasys::TimerSystem thread
        // and we want to quickly post this for processing
        // in order to release the TimerSystem to go back to its job
        first_call_ = false;
        parent_->Post_Timer_To_Process(this);

    } else {
        // do the real processing...
        string no_segment = "";

        int64_t right_now = parent_->AOS_Counter();

        if(right_now >= target_counter_)
        {
            if(parent_type_ == PARENT_SENDER) {
                log_debug("InactivityTimer.timeout: Session: %s - perform Sender cleanup processing",
                          session_key_.c_str());

                parent_->Cleanup_Session_Sender(session_key_, no_segment, 0);  
            } else {
                log_debug("InactivityTimer.timeout: Session: %s - perform Receiver cleanup processing",
                          session_key_.c_str());

                parent_->Cleanup_Session_Receiver(session_key_, no_segment, 0);
            }

            // Cleanup calls should have NULLed out the pointers to this object
            delete this;

        } else {  // invoke a new timer!
            int seconds = (int) (target_counter_ - right_now);

            log_debug("InactivityTimer.timeout: Session: %s - reschedule in %d seconds",
                      session_key_.c_str(), seconds);

            first_call_ = true; // reset the first call flag
            schedule_in(seconds*1000);
        }
    }
}

//----------------------------------------------------------------------
const char *
LTPUDPSession::Get_Session_State()
{
    const char * return_var = "";
    static char   temp[20];

    switch(session_state_)
    {
        case LTPUDPSession::LTPUDP_SESSION_STATE_RS:
            return_var = "Session State RS";
            break;

        case LTPUDPSession::LTPUDP_SESSION_STATE_CS:
            return_var = "Session State CS";
            break;

        case LTPUDPSession::LTPUDP_SESSION_STATE_DS:
            return_var = "Session State DS";
            break;

        case LTPUDPSession::LTPUDP_SESSION_STATE_UNKNOWN:
            return_var = "Session State UNKNOWN";
            break;

        case LTPUDPSession::LTPUDP_SESSION_STATE_UNDEFINED:
            return_var = "Session State UNDEFINED";
            break;

        default:
            sprintf(temp,"SessState?? (%d)",session_state_);
            return_var = temp; 
            break;
    }
    return return_var;
}

//
//   LTPUDPSegment ......................
//
LTPUDPSegment::LTPUDPSegment() :
        oasys::Logger("LTPUDPSegment",
                      "/dtn/cl/ltpudp/segment/%p", this)
{
    segment_type_             = 0;
    red_green_none_           = 0;
    timer_                    = 0;
    is_checkpoint_            = false;
    is_redendofblock_         = false;
    is_greenendofblock_       = false;
    retrans_retries_          = 0;
    parent_                   = (LTPUDPCallbackIF *) 0;
    is_security_enabled_      = false;
    log_debug("Segment C'Tor Cipher Enabled? %d\n",is_security_enabled_);
    security_mask_            = LTPUDP_SECURITY_DEFAULT;
    cipher_trailer_offset_    = 0;
    cipher_applicable_length_ = 0;
    cipher_key_id_            = -1;
    cipher_suite_             = -1;
    cipher_engine_            = "";

    //dzdebug
    version_number_           = 0;
    control_flags_            = 0;
    header_exts_              = 0;
    trailer_exts_             = 0;
    packet_len_               = 0;
    engine_id_                = 0;
    session_id_               = 0;
    serial_number_            = 0;
    checkpoint_id_            = 0;
    client_service_id_        = 0;
    is_valid_                 = true;
}

//----------------------------------------------------------------------
LTPUDPSegment::LTPUDPSegment(LTPUDPSegment& from) :
        oasys::Logger("LTPUDPSegment",
                      "/dtn/cl/ltpudp/segment/%p", this)

{
    key_                   = from.key_;
    session_key_           = from.session_key_;
    segment_type_          = from.segment_type_;
    engine_id_             = from.engine_id_;
    session_id_            = from.session_id_;
    serial_number_         = from.serial_number_;
    checkpoint_id_         = from.checkpoint_id_;
    parent_                = from.parent_;
    control_flags_         = from.control_flags_;
    retrans_retries_       = from.retrans_retries_;
    timer_                 = from.timer_;
    security_mask_         = from.security_mask_;
    is_checkpoint_         = from.is_checkpoint_;
    is_redendofblock_      = from.is_redendofblock_;
    is_greenendofblock_    = from.is_greenendofblock_;

    is_security_enabled_      = from.is_security_enabled_;
    log_debug("Segment C'Tor copy from input segment Cipher Enabled? %d\n",is_security_enabled_);



    version_number_             = from.version_number_;
    header_exts_                = from.header_exts_;
    trailer_exts_               = from.trailer_exts_;
    packet_len_                 = from.packet_len_;
    client_service_id_          = from.client_service_id_;
    cipher_applicable_length_   = from.cipher_applicable_length_;
    cipher_trailer_offset_      = from.cipher_trailer_offset_;
    cipher_suite_               = from.cipher_suite_;
    cipher_key_id_              = from.cipher_key_id_;
    cipher_engine_              = from.cipher_engine_;
    serial_number_str_          = from.serial_number_str_;
    is_valid_                   = from.is_valid_;
    red_green_none_             = from.red_green_none_;
}

//----------------------------------------------------------------------
LTPUDPSegment::LTPUDPSegment(LTPUDPSession * session) :
        oasys::Logger("LTPUDPSegment",
                      "/dtn/cl/ltpudp/segment/%p", this)
{
    key_                = "";  
    packet_len_         = 0;
    version_number_     = 0; 
    engine_id_          = session->Engine_ID();
    session_id_         = session->Session_ID();
    Set_Session_Key();
    serial_number_      = 0;  // this is a new Segment so RPT should be 0. 
    checkpoint_id_      = session->Checkpoint_ID();
    parent_             = session->Parent(); 
    control_flags_      = 0;
    segment_type_       = 0;
    client_service_id_  = 1;
    red_green_none_     = 0;
    is_checkpoint_      = false;
    is_redendofblock_   = false;
    is_greenendofblock_ = false;
    is_valid_           = true;
    retrans_retries_    = 0;
    memset(packet_.buf(1),0,1); /* Control Byte  initialize to zero and we'll change before we transmit/buffer */
    packet_.incr_len(1);
    timer_              = NULL;
    log_debug("Session is setting Segments Outbound Cipher suite=%d",session->Outbound_Cipher_Suite());
    log_debug("Session is setting Segments Outbound Cipher keyid=%d",session->Outbound_Cipher_Key_Id());
    log_debug("Session is setting Segments Outbound Cipher engine=%s", session->Outbound_Cipher_Engine().c_str());
    security_mask_       = LTPUDP_SECURITY_DEFAULT;
    cipher_key_id_       = session->Outbound_Cipher_Key_Id();
    cipher_engine_       = session->Outbound_Cipher_Engine();
    cipher_suite_        = session->Outbound_Cipher_Suite();
    is_security_enabled_ = ((int) cipher_suite_ != -1);
    log_debug("Session is setting Segments Cipher security enabled=%d",is_security_enabled_);
    cipher_applicable_length_ = 0;
    cipher_trailer_offset_ = 0;
    log_debug("LTPUTPSegment(session) c'tor: security enabled %d",is_security_enabled_);
    log_debug("LTPUTPSegment(session) c'tor: cipher_suite  : %d",cipher_suite_);
    log_debug("LTPUTPSegment(session) c'tor: cipher_key_id : %d",cipher_key_id_);
    log_debug("LTPUTPSegment(session) c'tor: cipher_engine : LTP:%s",cipher_engine_.c_str());


    //dzdebug
    header_exts_ = 0;
    trailer_exts_ = 0;
}

//----------------------------------------------------------------------
LTPUDPSegment::~LTPUDPSegment()
{
     raw_packet_.clear();
     packet_.clear();
}
//----------------------------------------------------------------------
void
LTPUDPSegment::Set_Security_Mask(u_int32_t new_val)
{
    security_mask_ = new_val;
}
//----------------------------------------------------------------------
void 
LTPUDPSegment::Set_Cipher_Suite(int new_val)
{
    log_debug("Setting Segment Cipher Suite %d\n",new_val);
    cipher_suite_ = new_val;
    is_security_enabled_ = (new_val != -1) ? true : false;
    log_debug("Setting Segment Cipher Enabled? %d\n",is_security_enabled_);
}
//----------------------------------------------------------------------
void
LTPUDPSegment::Set_Cipher_Key_Id(int new_key)
{
    log_debug("Setting Segment Cipher KeyID %d\n",new_key);
    cipher_key_id_ = new_key;
}
//----------------------------------------------------------------------
void 
LTPUDPSegment::Set_Cipher_Engine(string new_engine)
{
    log_debug("Setting Segment Cipher Engine %s\n",new_engine.c_str());
    cipher_engine_ = new_engine;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::Encode_Field(u_int64_t infield)
{
    int estimated_len = SDNV::encoding_len(infield); 
    packet_len_  += SDNV::encode(infield,packet_.tail_buf(estimated_len), sizeof(infield));
    packet_.incr_len(estimated_len);
} 

//----------------------------------------------------------------------
void
LTPUDPSegment::Encode_Field(u_int32_t infield)
{
    int estimated_len = SDNV::encoding_len(infield); 
    packet_len_  += SDNV::encode(infield,packet_.tail_buf(estimated_len), sizeof(infield));
    packet_.incr_len(estimated_len);
} 

//----------------------------------------------------------------------
void 
LTPUDPSegment::Set_Client_Service_ID(u_int32_t in_client_service_id)
{
     client_service_id_  = in_client_service_id;
}

//----------------------------------------------------------------------
int LTPUDPSegment::Parse_Buffer(u_char * bp, int length, int * byte_offset)
{
    u_int32_t test_tag  = 0;
    u_int32_t test_len  = 0;
    u_int32_t seg_len   = 0;

    int       read_len  = 0;
    int current_byte    = 1;
    int decode_status   = 0;
    bool header_found   = false;

    control_flags_      = (int) (*bp & 0x0F);

    is_redendofblock_   = false;
    is_greenendofblock_ = false;
    is_checkpoint_      = false;
    is_valid_           = true;
    red_green_none_     = 0;

    timer_              = 0;

    log_debug("Parse_Buffer: Control Flags = %02X",control_flags_);

    bool ctr_flag = (control_flags_ & 0x08) > 0 ? true : false;
    bool exc_flag = (control_flags_ & 0x04) > 0 ? true : false;
    bool flag_1   = (control_flags_ & 0x02) > 0 ? true : false;
    bool flag_0   = (control_flags_ & 0x01) > 0 ? true : false;
   
    // Written from PAGE 11 of LTP Spec RFC 5326  
    if(!ctr_flag) { // Data Segment
        if(!exc_flag) { // Red Data Segment
            red_green_none_ = RED_SEGMENT;
            if(flag_1) {
                is_checkpoint_      = true;
                if(flag_0) {
                   is_redendofblock_  = true;
                }
            } else { // not end of red-part
                if(flag_0)is_checkpoint_ = true;
            }
            log_debug("Parse_Buffer: RedEndofblock (%d) Checkpoint (%d)",is_redendofblock_,is_checkpoint_);  
        } else { // Green Data Segment
            red_green_none_ = GREEN_SEGMENT;
            if(flag_1) {
                is_checkpoint_      = true;
                if(flag_0) {
                    is_greenendofblock_ = true;
                }
            }
            log_debug("Parse_Buffer: GreenEndofblock (%d)",is_greenendofblock_);  
        }
    }

    version_number_    = (int) (*bp & 0xF0) >> 4;

    // this will probably need to create an LTP thing....
    // Or at least parse off the front end to get the session and source. 
    //  Then add it to an existing session map or create a new one then add.

    // this should pull the engine id from and returns bytes used
    decode_status = SDNV::decode(bp+current_byte, length-current_byte, &engine_id_);

    CHECK_DECODE_STATUS

    log_debug("Parse_Buffer: Engine ID = %"PRIu64, engine_id_);

    // this should pull the session id from the buffer
    decode_status = SDNV::decode(bp+current_byte, length-current_byte, &session_id_); 

    CHECK_DECODE_STATUS

    log_debug("Parse_Buffer: Session ID = %"PRIu64, session_id_);

    Set_Session_Key();

    // pull off each nibble and make int.
    //
    char one_byte = (char) * (bp+current_byte);

    if (one_byte > 0) { // may have only a trailer too
        is_security_enabled_ = true;
    }

    header_exts_  = (int) (one_byte & 0xF0) >> 4;
    trailer_exts_ = (int) (one_byte & 0x0F);
 
    is_security_enabled_ = (one_byte > 0x00);
    current_byte += 1;  // we just processed a byte

    log_debug("Parse_Buffer: One Byte HDR/TRL = %02X current_byte %d",one_byte,current_byte); 
    log_debug("Parse_Buffer: One Byte HDR/TRL = header_exts_=%d trailer_exts_=%d",header_exts_,trailer_exts_); 

    for(int ctr = 0 ; ctr < header_exts_ ; ctr++) {
        log_debug("Parse_Buffer: Reading HDR = %d",ctr); 
        test_tag = bp[current_byte];
        current_byte += 1; // segtag   
        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &test_len);
        CHECK_DECODE_STATUS
        if(test_tag == 0x00)
        {
            log_debug("Parse_Buffer: Found LTPUDP_AUTH cb=%d", current_byte); 
            if (!header_found) {
                seg_len              = test_len;
                cipher_suite_        = bp[current_byte];
                cipher_key_id_       = -1;
                current_byte        += 1; // segtag   
                log_debug("Parse_Buffer: Found suite=%d cb=%d", cipher_suite_, current_byte); 
                read_len             = seg_len - 1;
                is_security_enabled_ = true;
                header_found         = true;
                log_debug("Parse_Buffer: read_len = %d",read_len); 
                if(read_len > 0)
                {
                    decode_status = SDNV::decode(bp+current_byte, length-current_byte, &cipher_key_id_); 
                    CHECK_DECODE_STATUS
                    log_debug("Parse_Buffer: cipher_key_id = %d",cipher_key_id_); 
                }
            } else {
                log_debug("Parse_Buffer: more than one LTPUDP_AUTH header found");  
                current_byte += seg_len; // skip the data
            }
        } else {
            current_byte += seg_len; // skip the data
        }
        log_debug("Parse_Buffer: About to go to next header_ext");
    }
    log_debug("Parse_Buffer: setting byte_offset cb=%d  Data= %02X", current_byte, bp[current_byte]); 

    *byte_offset = current_byte;

    if(current_byte > length)is_valid_ = false;
    return 0;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::Set_Session_Key()
{
    char temp[45];
    sprintf(temp, "%20.20"PRIu64":%20.20"PRIu64, engine_id_, session_id_);
    session_key_ = temp;
}

//----------------------------------------------------------------------
void 
LTPUDPSegment::Set_RGN(int new_color)
{
    if(new_color == GREEN_SEGMENT) {
        control_flags_ = control_flags_ | 0x04;
    } else if(new_color == RED_SEGMENT) {
        control_flags_ = control_flags_ & 0x0B;
    }
    red_green_none_ = new_color;
}

//----------------------------------------------------------------------
void 
LTPUDPSegment::Set_Report_Serial_Number(u_int64_t new_rsn)
{
    serial_number_ = new_rsn;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::Set_Checkpoint_ID(u_int64_t new_chkpt)
{
    checkpoint_id_ = new_chkpt;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::SetCheckpoint()
{
    u_char control_byte = (u_char) (control_flags_ & 0xFF);
    if(packet_.len() == 0) {
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    } else {
        memset(packet_.buf(), control_byte, 1); // allocated by Constructor... 
    }
    is_checkpoint_ = true;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::UnsetCheckpoint()   //XXX/dz - what is the difference with SetCheckpoint? Are these used??
{
    u_char control_byte = (u_char) (control_flags_ & 0xFF);
    if(packet_.len() == 0) {
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    } else {
        memset(packet_.buf(), control_byte, 1); // allocated by Constructor... 
    }
    is_checkpoint_ = false;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::Set_Parent(LTPUDPCallbackIF * parent)
{
    parent_ = parent;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::Set_Retrans_Retries(int new_count)
{
    retrans_retries_ = new_count;
}

//----------------------------------------------------------------------
void
LTPUDPSegment::Increment_Retries()
{
    retrans_retries_++;
    //log_debug("LTPUDPSegment::Increment_Retries: %d",retrans_retries_);
}

//----------------------------------------------------------------------
void
LTPUDPSegment::Create_LTP_Header()
{
    u_char control_byte = (u_char) (control_flags_ & 0xFF);
    if(packet_.len() == 0) {
        log_debug("CLTPH:Adding a byte for the header!! ");
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    } else {
        log_debug("CLTPH:replacing the byte in the header!!  %d",(int) packet_.len());
        memset(packet_.buf(), control_byte, 1); // allocated by Constructor... 
        packet_.set_len(1);
    }

    log_debug("CLTPH:Control Byte is = %X Segment Type=%s",control_byte,Get_Segment_Type());
    /*
     * stuff the session_ctr into the buffer
     */
    packet_len_ = 1;
    log_debug("CLTPH: byte encode_length %d actual packet len = %d", packet_len_,(int) packet_.len());

    Encode_Field(engine_id_);
    //Dump_Current_Buffer("After Engine_ID");

    log_debug("CLTPH: engine id %"PRIu64" packet_len = %d",engine_id_,packet_len_);

    Encode_Field(session_id_);
    //Dump_Current_Buffer("After Session_ID");

    log_debug("CLTPH: session_ctr %"PRIu64" packet_len = %d",session_id_,packet_len_);
    /*
     *  HDR / TRLR ext we don't have to set to 0x00;
     */
    log_debug("CLTPH: before Sgement Security is enabled = %d!!\n",is_security_enabled_);
#ifdef LTPUDP_AUTH_ENABLED

    if(is_security_enabled_)
    {
        if(security_mask_ == LTPUDP_SECURITY_DEFAULT)security_mask_ = LTPUDP_SECURITY_HEADER | LTPUDP_SECURITY_TRAILER;
        log_debug("CLTPH: Checking Sgement Security is enabled = %d mask=%02X!!n",is_security_enabled_,security_mask_);
        if((security_mask_ & LTPUDP_SECURITY_HEADER) ==  0 && (security_mask_ & LTPUDP_SECURITY_TRAILER) > 0) 
        {
            memset(packet_.tail_buf(1),0x01,1);
        } else {
            memset(packet_.tail_buf(1),0x11,1); // when in doubt turn both on.
        }
    } else  
#endif
        memset(packet_.tail_buf(1),0,1); // plain old no hdr and no trlr

    packet_.incr_len(1);
    packet_len_ ++;

#ifdef LTPUDP_AUTH_ENABLED

//   ext  tag  sdnv c-s  k-id
//  +----+----+----+----+----+
//  |0x11|0x00|0x02|0x00|0x24|
//  +----+----+----+----+----+

    if(is_security_enabled_)
    {
       if (security_mask_ == LTPUDP_SECURITY_DEFAULT || 
          (security_mask_ &  LTPUDP_SECURITY_HEADER) > 0) {

           log_debug("security_mask = %02X",security_mask_);

           log_debug("CLTPHeader: before Build Header EXT packet_len_=%d",packet_len_);
           Build_Header_ExtTag(); 
       }
    }

#endif
    
    log_debug("CLTPHeader: After hdr / trlr packet_len_=%d",packet_len_);
    return;
}
void
LTPUDPSegment::Build_Header_ExtTag()
{
    int estimated_len = 0;
    log_debug("Build_Header_ExtTag");
    memset(packet_.tail_buf(1),0,1); // LTP AUTH
    packet_.incr_len(1);
    packet_len_ ++;
   
    if ((int) cipher_suite_ == 0) {
        estimated_len = SDNV::encoding_len(cipher_key_id_); 
    }

    log_debug("BHET: Putting length %d\n",estimated_len+1);
    memset(packet_.tail_buf(1),((estimated_len+1) & 0xFF),1); // Ciphersuite
    packet_.incr_len(1);
    packet_len_ ++;

    log_debug("BHET: Putting Suite=%d\n",cipher_suite_);
    memset(packet_.tail_buf(1),(cipher_suite_ & 0xFF),1); // Ciphersuite
    packet_.incr_len(1);
    packet_len_ ++;

    if(estimated_len > 0)
    {
       log_debug("BHET: Putting each byte into byte array! key_id=%d SDNV length %d",cipher_key_id_,estimated_len);
        LTPUDPSegment::Encode_Field((u_int32_t)cipher_key_id_); // SDNV the key_id
    }
}
//----------------------------------------------------------------------
LTPUDPSegment * LTPUDPSegment::DecodeBuffer(u_char * bp, size_t length)
{
    int control_flags    = (int) (*bp & 0x0F);

    bool ctr_flag = (control_flags & 0x08) > 0 ? true : false;
    bool exc_flag = (control_flags & 0x04) > 0 ? true : false;
    bool flag_1   = (control_flags & 0x02) > 0 ? true : false;
    bool flag_0   = (control_flags & 0x01) > 0 ? true : false;

    if(!ctr_flag)  // 0 * * *
    {
       return new LTPUDPDataSegment(bp,(int) length);
    }
    else // REPORT, CANCEL segment   1 * * *
    {
       if(!exc_flag) {       // exc_flag == 0  REPORT SEGMENT     1 0 * *
           if(!flag_1) {     // flag_1   == 0                     1 0 0 *
               if(!flag_0) { // flag_0   == 0                     1 0 0 0
                   return new LTPUDPReportSegment(bp,(int) length);
               } else {      // flag_0   == 1                     1 0 0 1
                   return new LTPUDPReportAckSegment(bp,(int) length);
               }
           } else {
               // not implemented yet....
           }
       } else {               // exc_flag == 1                    1 1 * *
           if (flag_1) {      // flag_1   == 1                    1 1 1 *
               if(!flag_0) {  // flag_0   == 0                    1 1 1 0 CS from block RCVR
                   return new LTPUDPCancelSegment(bp,(int) length, LTPUDPSegment::LTPUDP_SEGMENT_CS_BR);
               } else {       // flag_0   == 1  //                1 1 1 1 CAS to Block RCVR
                   return new LTPUDPCancelAckSegment(bp,(int) length,  LTPUDPSegment::LTPUDP_SEGMENT_CAS_BR);
               }
           } else {           // flag_1 == 0                      1 1 0 *
               if(!flag_0) {   // flag_0 == 0  CS to block sender  1 1 0 0
                   return new LTPUDPCancelSegment(bp,(int) length,  LTPUDPSegment::LTPUDP_SEGMENT_CS_BS);
               } else {      // flag_0 == 1                       1 1 0 1    CAS from block sender 
                   return new LTPUDPCancelAckSegment(bp,(int) length,  LTPUDPSegment::LTPUDP_SEGMENT_CAS_BS);
               }
           }
       }
    }
    return NULL;
}
//----------------------------------------------------------------------
const char * LTPUDPSegment::Get_Segment_Type()
{
    return LTPUDPSegment::Get_Segment_Type(this->segment_type_);
}

//----------------------------------------------------------------------
const char * LTPUDPSegment::Get_Segment_Type(int st)
{
    const char * return_var = "";
    static char temp[20];
 
    switch(st)
    {
        case LTPUDPSegment::LTPUDP_SEGMENT_UNKNOWN:
            return_var = "Segment Type UNKNOWN(0)";
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_DS:
            return_var = "Segment Type DS(1)";
            break;
    
        case LTPUDPSegment::LTPUDP_SEGMENT_RS:
            return_var = "Segment Type RS(2)";
            break;
    
        case LTPUDPSegment::LTPUDP_SEGMENT_RAS:
            return_var = "Segment Type RAS(3)";
            break;
    
        case LTPUDPSegment::LTPUDP_SEGMENT_CS_BR:
            return_var = "Segment Type CS_BR(4)"; 
            break;
    
        case LTPUDPSegment::LTPUDP_SEGMENT_CAS_BR:
            return_var = "Segment Type CAS_BR(5)";
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_CS_BS:
            return_var = "Segment Type CS_BS(6)"; 
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_CAS_BS:
            return_var = "Segment Type CAS_BS(7)";
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_CTRL1:
            return_var = "Segment Type CTRL1(8)"; 
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_CTRL2:
            return_var = "Segment Type CTRL2(9)"; 
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_UNDEFINED:
            return_var = "Segment Type UNDEFINED(10)";
            break;
   
        case LTPUDPSegment::LTPUDP_SEGMENT_DS_TO:
            return_var = "Segment Type DS(11) timeout";
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_RS_TO:
            return_var = "Segment Type RS(12) timeout";
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_CS_TO:
            return_var = "Segment Type CS(13) timeout"; 
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_COMPLETED:
            return_var = "Segment Type COMPLETED(15)"; 
            break;

        default:
            sprintf(temp,"SegType? (%d)",st);
            return_var = temp;
            break;
    }
    return return_var;
}

//--------------------------------------------------------------------------
void
LTPUDPSegment::Create_RS_Timer(int seconds, int parent_type, int segment_type, u_int64_t report_serial_number)
{
    // initialize the retrans timer
    if (timer_ != 0) {
        timer_->cancel();
        timer_ = 0;
    }
    timer_   = new RetransmitTimer(seconds, parent_, parent_type, segment_type, this->session_key_, report_serial_number, (void**)&timer_, this);

    log_debug("Create_RS_Timer: Session: %s  secs: %d  rpt serial#: %"PRIu64"  segment: %s  timer: %p",
              session_key_.c_str(), seconds, report_serial_number, Get_Segment_Type(), timer_);
}

//--------------------------------------------------------------------------
void
LTPUDPSegment::Start_Retransmission_Timer(void* timer)
{
    // start the retrans timer
    if (timer_ == 0) return;
    if (timer_ != timer) {
        log_debug("Start_Retransmission_Timer - timer does not match - ignoring  %s", session_key_.c_str());
        return;
    }

    log_debug("Start_Retransmission_Timer: Session: %s  segment: %s (%s)  secs: %d  timer: %p", 
              session_key_.c_str(), key_.c_str(), Get_Segment_Type(), timer_->seconds_, timer);

    if (timer_->pending()) {
        //dzdebug
        log_crit("Start_Retransmission_Timer - timer_.pending is TRUE!!! %s -- retrans:%d - not restarting!!", 
                 session_key_.c_str(), retrans_retries_ );
        return;
    }

    timer_->SetSeconds(timer_->seconds_); // re-figure the target_counter
    timer_->schedule_in(timer_->seconds_*1000); // it needs milliseconds
}

//--------------------------------------------------------------------------
void
LTPUDPSegment::Create_Retransmission_Timer(int seconds, int parent_type, int segment_type)
{
    // initialize the retrans timer
    if (timer_ != 0) {
        timer_->cancel();
        timer_ = 0;
    }
    timer_   = new RetransmitTimer(seconds, parent_, parent_type, segment_type, this->session_key_, this->key_, (void**)&timer_, this);

    log_debug("Create_Retransmission_Timer: Session: %s  segment: %s (%s)  timer: %p", 
              session_key_.c_str(), key_.c_str(), Get_Segment_Type(), timer_);
}

//--------------------------------------------------------------------------
void    
LTPUDPSegment::Cancel_Retransmission_Timer()
{
    if(timer_ == 0)return;

    if(timer_->pending() || !timer_->cancelled())
    {
        bool cancelled = timer_->cancel();
        if(cancelled) {
            log_debug("Cancel_Retransmission_Timer: Session: %s segment: %s (%s)  cancelled timer: %p",
                      session_key_.c_str(), key_.c_str(), Get_Segment_Type(), timer_);
        }
    }
    timer_=0;
}

//--------------------------------------------------------------------------
void    
LTPUDPSegment::RetransmitTimer::timeout(const struct timeval &now)
{
    (void)now;
    if (first_call_) {
        log_debug("RetransmitTimer::timeout on Segment(%d) Segment_Key(%s) Session_key(%s) Serial_Numb(%"PRIu64") Seg(%s) - post to process",
                  segment_type_, segment_key_.c_str(), session_key_.c_str(), report_serial_number_,
                  LTPUDPSegment::Get_Segment_Type(segment_type_));

        // first call is performed in the oasys::TimerSystem 
        // and we want to quickly post this for processing
        // in order to release the TimerSystem to go back to its job
        first_call_ = false;
        parent_->Post_Timer_To_Process(this);

    } else {
        // do the real processing...
        log_debug("RetransmitTimer::timeout on Segment(%d) Segment_Key(%s) Session_key(%s) Serial_Numb(%"PRIu64") Seg(%s) - processing",
                  segment_type_, segment_key_.c_str(), session_key_.c_str(), report_serial_number_,
                  LTPUDPSegment::Get_Segment_Type(segment_type_));

        u_int64_t right_now = parent_->AOS_Counter();

        if(right_now >= target_counter_) {
            if(parent_type_ == PARENT_SENDER) {
                log_debug("RetransmitTimer::timeout - calling Cleanup_Session_Sender(%s,%s:%d) Seg(%s)",
                          session_key_.c_str(), segment_key_.c_str(), segment_type_,
                          LTPUDPSegment::Get_Segment_Type(segment_type_));

                parent_->Cleanup_Session_Sender(session_key_, segment_key_, 
                                                segment_type_);
            } else {
                if(report_serial_number_ > 0) {
                    log_debug("RetransmitTimer::timeout - calling Cleanup_RS_Receiver(SESS:%s RSN:%s SEGTYPE:%d(%s)) ",
                              session_key_.c_str(), segment_key_.c_str(),
                              segment_type_, LTPUDPSegment::Get_Segment_Type(segment_type_));

                    parent_->Cleanup_RS_Receiver(session_key_, report_serial_number_, 
                                                 segment_type_);
                } else {
                    log_debug("RetransmitTimer::timeout - calling Cleanup_Session_Receiver(SESS:%s SEG:%s SEGTYPE:%s) ",
                              session_key_.c_str(), segment_key_.c_str(),
                              LTPUDPSegment::Get_Segment_Type(segment_type_));

                    parent_->Cleanup_Session_Receiver(session_key_, segment_key_, 
                                                      segment_type_);
                }
            }

            // Cleanup calls should have NULLed out the pointers to this object
            delete this;

        } else {  // invoke a new timer!
            log_debug("RetransmitTimer::timeout - target counter not hit - now(%"PRIu64") target(%"PRIu64") Seg(%s) - rescheduling", 
                      right_now,target_counter_, LTPUDPSegment::Get_Segment_Type(segment_type_));

            int seconds = (int) (target_counter_ - right_now);
            first_call_ = true; // reset the first call flag
            schedule_in(seconds*1000);
        }
    }
}
#ifdef LTPUDP_AUTH_ENABLED
//----------------------------------------------------------------------
void
LTPUDPSegment::Create_Trailer()
{
    BIO      * key_bio  = NULL;
    EVP_PKEY * skey     = NULL;
    u_char   * sig      = NULL;
    size_t     slen     = 0;

    HMAC_CTX ctx;

    // take the entire current segment and perform cipher

    int ltp_auth = 0;

    log_debug("Inside Create Trailer packet_len_ = %d",packet_len_);

    if ((int) cipher_suite_ == -1) {
        log_debug("Create_Trailer: invalid cipher_suite = %d",cipher_suite_);
        return;
    }

    u_char * cipher_buffer = (u_char *) 0;

    int result_length = suite_result_len(cipher_suite_);

    const KeyDB::Entry* key_entry = NULL;

    if((int) cipher_suite_ == -1)return;
//
//  lookup KeyDB if one specified and cipher_suite == 0... 
//
    if(cipher_suite_ == 0)
    {
        key_entry = KeyDB::find_key(cipher_engine_.c_str(), cipher_suite_, cipher_key_id_);
        if (key_entry == NULL) {
             log_debug("Create_Trailer: Key not found engine(LTP:%s:%d) cipher_suite = %d",
                       cipher_engine_.c_str(), cipher_key_id_, cipher_suite_);
             return;
        }
    }

    switch (result_length) {
        //
        // 20 is NULL(255) or ciphersuite 0
        //
        case 20:

            HMAC_CTX_init(&ctx);

            if (cipher_suite_ == 255) {
                HMAC_Init_ex(&ctx, HMAC_NULL_CIPHER_KEY, result_length, EVP_sha1(), NULL); // what goes where EVP_sha1 is if NULL
            } else if(cipher_suite_ == 0) { 
                HMAC_Init_ex(&ctx, key_entry->key(), key_entry->key_len(), EVP_sha1(), NULL);
            } else {
                log_err("Create_Trailer: cipher_suite Invalid %d ", cipher_suite_);
                HMAC_cleanup(&ctx);
                return;
            }
            log_debug("Create_Trailer: HMAC this stuff to length %d",packet_len_);
            HMAC_Update(&ctx, packet_.buf(), packet_len_);

            cipher_buffer = (u_char *) malloc(sizeof(char) * result_length);
    
            if (cipher_buffer) {
                HMAC_Final(&ctx, cipher_buffer,(unsigned int *) &result_length); // I think this should build the cipher for the packet 
                HMAC_cleanup(&ctx);
            }
            else 
                log_err("unable to malloc cipher_buffer");
            break;

        case 128:
//
// 128 is ciphersuite 1
//
            char filename[1024];
            sprintf(filename,"%s/ltp_%d_%s_%d_privkey.pem", Ciphersuite::config->privdir.c_str(), 
                    cipher_suite_,cipher_engine_.c_str(),cipher_key_id_);
        
            log_debug("Opening %s",filename); 

            key_bio = BIO_new_file(filename,"r");

            if (key_bio) {
            
                skey = PEM_read_bio_PrivateKey(key_bio, NULL, 0, NULL);

                BIO_free(key_bio);
        
                if (skey) {

//dzdebug - moved above - will not be freed if skey == NULL                     BIO_free(key_bio);
        
                    int rc = sign_it((u_char *) packet_.buf(), packet_len_, &sig, &slen, skey);
                    if(rc == 0) {
                        log_debug("Created signature  slen=%zu", slen);
                    } else {
                        log_err("Failed to create signature, return code %d", rc);
                        if (sig) OPENSSL_free(sig);
                        if (skey) EVP_PKEY_free(skey);
                        return; /* Should cleanup here */
                    }

                } else  {
                    log_err("Error reading private key file: %s", filename);
                    return;
                }
            } else {
                log_err("Error opening private key file: %s", filename);
                return;
            }
            break;
    
        default: 

            log_err("Create_Trailer::cipher_suite Invalid value for hash length (bytes): %d (should be 20, 32 or 48)",
                    result_length);
            return;
            break;
    }

//
//  Insert trailer into the Packet... 
//
    memcpy(packet_.tail_buf(1),&ltp_auth,1);
    packet_.incr_len(1);
    packet_len_    += 1;

    switch(cipher_suite_) {

        case 0:
        case 255:
    
            if(result_length == 20)result_length = 10; // only parse the first 10 bytes
            log_debug("Create_Trailer: Encoding %d into Ext Trlr",result_length);  
            LTPUDPSegment::Encode_Field((u_int32_t)result_length);
            memcpy(packet_.tail_buf(result_length),cipher_buffer,result_length);
            packet_.incr_len(result_length);
            packet_len_    += result_length;
            if(cipher_buffer != (u_char *) 0)free(cipher_buffer);
            HMAC_cleanup(&ctx);
            break;

       case 1:
 
           LTPUDPSegment::Encode_Field((u_int32_t)slen);
           memcpy(packet_.tail_buf(slen),sig,slen);
           packet_.incr_len(slen);
           packet_len_    += slen;
           if (sig) OPENSSL_free(sig);
           if (skey) EVP_PKEY_free(skey);
           break;

       default:

           log_err("Create_Trailer: invalid cipher suite value : %d",cipher_suite_);
           break;
    }

    log_debug("Create Trailer: Exiting packet_len_ = %d",packet_len_);
}

//----------------------------------------------------------------------
int LTPUDPSegment::sign_it(const u_char* msg, size_t mlen, u_char** sig, size_t* slen, EVP_PKEY* pkey)
{
    /* Returned to caller */
    int result = -1;

    if(!msg || !mlen || !sig || !pkey) {
        return -1;
    }

    if(*sig)
        OPENSSL_free(*sig);

    *sig = NULL;
    *slen = 0;

    EVP_MD_CTX* ctx = NULL;

    do
    {
        ctx = EVP_MD_CTX_create();
        if(ctx == NULL) {
            log_err("EVP_MD_CTX_create failed, error 0x%lx", ERR_get_error());
            break; /* failed */
        }

        int rc = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        if(rc != 1) {
            log_err("EVP_DigestInit_ex failed, error 0x%lx", ERR_get_error());
            break; /* failed */
        }

        rc = EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, pkey);
        if(rc != 1) {
            log_err("EVP_DigestSignInit failed, error 0x%lx", ERR_get_error());
            break; /* failed */
        }

        rc = EVP_DigestSignUpdate(ctx, msg, mlen);
        if(rc != 1) {
            log_err("EVP_DigestSignUpdate failed, error 0x%lx", ERR_get_error());
            break; /* failed */
        }

        size_t req = 0;
        rc = EVP_DigestSignFinal(ctx, NULL, &req);
        if(rc != 1) {
            log_err("EVP_DigestSignFinal failed (1), error 0x%lx", ERR_get_error());
            break; /* failed */
        }

        if(!(req > 0)) {
            log_err("EVP_DigestSignFinal failed (2), error 0x%lx", ERR_get_error());
            break; /* failed */
        }

        *sig = (u_char *) OPENSSL_malloc(req);
        if(*sig == NULL) {
            log_err("OPENSSL_malloc failed, error 0x%lx", ERR_get_error());
            break; /* failed */
        }

        *slen = req;
        rc = EVP_DigestSignFinal(ctx, *sig, slen);
        if(rc != 1) {
            log_err("EVP_DigestSignFinal failed (3), return code %d, error 0x%lx", rc, ERR_get_error());
            break; /* failed */
        }

        if(rc != 1) {
            log_err("EVP_DigestSignFinal failed, mismatched signature sizes %zu, %zu", req, *slen);
            break; /* failed */
        }

        result = 0;

    } while(0);

    if(ctx) {
        EVP_MD_CTX_destroy(ctx);
        ctx = NULL;
    }

    return !!result;
}

//----------------------------------------------------------------------
int LTPUDPSegment::verify_it(const u_char* msg, size_t mlen, const u_char* sig, size_t slen, EVP_PKEY* pkey)
{
    /* Returned to caller */
    int result = -1;
   
    if(!msg || !mlen || !sig || !slen || !pkey) {
        return -1;
    }
 
    EVP_MD_CTX* ctx = NULL;
   
    do
    {
        ctx = EVP_MD_CTX_create();
        if(ctx == NULL) {
            log_err("EVP_MD_CTX_create failed, error 0x%lx\n", ERR_get_error());
            break; /* failed */
        }
        log_debug("After CTX create");  
    
        int rc = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        if(rc != 1) {
            log_err("EVP_DigestInit_ex failed, error 0x%lx\n", ERR_get_error());
            break; /* failed */
        }
        log_debug("After get digestname");  
    
        rc = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey);
        if(rc != 1) {
            log_err("EVP_DigestVerifyInit failed, error 0x%lx\n", ERR_get_error());
            break; /* failed */
        }
        log_debug("After DigestVerifyInit");  

        rc = EVP_DigestVerifyUpdate(ctx, msg, mlen);
        if(rc != 1) {
            log_err("EVP_DigestVerifyUpdate failed, error 0x%lx\n", ERR_get_error());
            break; /* failed */
        }
        log_debug("After DigestVerifyUpdate");  
    
        /* Clear any errors for the call below */
        ERR_clear_error();
    
        rc = EVP_DigestVerifyFinal(ctx, (u_char*)sig, slen);
        if(rc != 1) {
            log_err("EVP_DigestVerifyFinal failed, error 0x%lx\n", ERR_get_error());
            break; /* failed */
        }

        log_debug("After DigestVerifyFinal");  
        result = 0;

    } while(0);

    if(ctx) {
        EVP_MD_CTX_destroy(ctx);
        ctx = NULL;
    }

    return !!result;
}

//----------------------------------------------------------------------
bool
LTPUDPSegment::IsAuthenticated(u_int32_t suite, u_int32_t key_id, string engine)
{
    HMAC_CTX ctx;
    log_debug("Inside IsAuthenticated! %d, %d, %s\n",suite,key_id,engine.c_str());

    u_char * cipher_buffer = (u_char *) 0;

    log_debug("IsAuthenticated suite:%d key_id:%d engine:%s",suite, key_id, engine.c_str());
    if ((int) cipher_suite_ != -1) {
        if (cipher_suite_ != suite) {
           log_debug("Authenticated failed suite=%d didn't match inbound suite=%d",cipher_suite_,suite);
           return false; 
        }
    }
    if ((int) cipher_key_id_ != -1) {
        if (cipher_key_id_ != key_id) {
           log_debug("IsAuthenticated failed key id=%d didn't match inbound key id=%d",cipher_key_id_,key_id);
           return false; 
        }
    }

    cipher_suite_  = suite;
    cipher_key_id_ = key_id;
    cipher_engine_ = engine;

    if(cipher_trailer_offset_ == 0)
    {
        log_err("IsAuthenticated No Cipher Trailer found in Segment expected %d",(int) suite);
        return false;
    }

    // grab the current segment and perform cipher.
    // grab the trailer data portion and compare...

    int result_length = suite_result_len(suite);
    
    const KeyDB::Entry* key_entry = NULL;

    switch (result_length) {

        case 20:

            HMAC_CTX_init(&ctx);
            if(cipher_suite_ == 0)
            {
                key_entry = KeyDB::find_key(engine.c_str(), suite, key_id);
                if (key_entry == NULL) {
                    log_debug("IsAuthenticated: Key not found engine(LTP:%s:%d) cipher_suite = %d",
                              cipher_engine_.c_str(), cipher_key_id_, cipher_suite_);
                    HMAC_cleanup(&ctx);
                    return false;
                }
            } 

            if (cipher_suite_ == 255) {
                HMAC_Init_ex(&ctx, HMAC_NULL_CIPHER_KEY, result_length, EVP_sha1(), NULL); // what goes where EVP_sha1 is if NULL
            } else  if(cipher_suite_ == 0) { 
                HMAC_Init_ex(&ctx, key_entry->key(), key_entry->key_len(), EVP_sha1(), NULL);
            } else {
                log_err("IsAuthenticated::cipher_suite Invalid %d ", suite);
                HMAC_cleanup(&ctx);
                return false;
            }

            cipher_buffer = (unsigned char*)malloc(sizeof(char) * result_length);

            log_debug("IsAuthenticated: HMAC the stuff for length = %zu applicable_length=%d",
                      raw_packet_.len()-cipher_trailer_offset_,
                      cipher_applicable_length_);

            if (cipher_buffer) {
                HMAC_Update(&ctx, raw_packet_.buf(), cipher_applicable_length_);
                HMAC_Final(&ctx, cipher_buffer,(unsigned int *) &result_length);  
            } else  {
                HMAC_cleanup(&ctx);
                log_err("IsAuthenticated: unable to malloc cipher_buffer");
                return false;
            }

            HMAC_cleanup(&ctx);

            if(result_length == 20)result_length = 10; // only check the first 10 bytes
//
//  Compare calculated cipher to Packet Trailer... 
//
            log_debug("IsAuthenticated: Cipher TO=%d raw_packet len=%d result_len=%d",
                      cipher_trailer_offset_, (int) raw_packet_.len(), result_length);
    
            log_debug("Checking packet trailer(%d) against result_length(%d)\n",
                  (int) (raw_packet_.len()-cipher_trailer_offset_),(int) result_length);
    
            if((int) (raw_packet_.len()-cipher_trailer_offset_) == result_length) {
                for(int i= 0; i < result_length ; i++)
                    log_debug("IsAuthenticated: Cipher[%02X] Packet[%02X]",
                              cipher_buffer[i],*(raw_packet_.buf()+cipher_trailer_offset_+i));
        
                if(memcmp(cipher_buffer,(raw_packet_.buf()+cipher_trailer_offset_),result_length) != 0)
                {
                    is_valid_ = false;
                    free(cipher_buffer);
                    return false;
                }
                log_debug("IsAuthenticated: Passes IsAuthenticated()");
            } else {
                log_err("IsAuthenticated: Invalid length of cipher value: %zu expected %d",
                        raw_packet_.len()-cipher_trailer_offset_,result_length);
            } 
//
// clean up the buffer
//
            if(cipher_buffer != (u_char *) 0)free(cipher_buffer);
            break;

        case 128: 

            {
           /* Sign and Verify HMAC keys */

            EVP_PKEY * vkey = NULL;
            X509     * cert = NULL;

            char filename[1024];

            sprintf(filename,"%s/ltp_%d_%s_%d_cert.pem", Ciphersuite::config->certdir.c_str(), 
                    cipher_suite_,cipher_engine_.c_str(),cipher_key_id_);

            // Read in the public key for verifying the signature
            log_debug("Opening %s",filename); 

            BIO* key_bio = BIO_new_file(filename, "r");

            if (key_bio) {

                cert = PEM_read_bio_X509(key_bio, NULL, 0, NULL);

                BIO_free(key_bio);

                if (cert) {

                    vkey = X509_get_pubkey(cert);

                    log_debug("verify_it applicable length=%d result_length=%d",cipher_applicable_length_,result_length);
               
                    if (vkey) {

    	                int rc = verify_it(raw_packet_.buf(), cipher_applicable_length_, 
                               raw_packet_.buf()+cipher_trailer_offset_, 
                                   result_length, vkey);

                        log_debug("return from verify = %d", rc);

                        if (rc != 0) {
                            is_valid_  = false;
                            log_err("IsAuthenticated failed Cipher Suite verification....");
                        }
                    } else 
                        log_err("Error extracting public key from certificate");
                } else 
                    log_err("Error reading public key (certificate)");
            } else 
                  log_err("Error reading cert");

            if(cert)X509_free(cert);
            if(vkey)EVP_PKEY_free(vkey);
            if(!is_valid_)return false;
            break;
            }

        default:
            log_err("IsAuthenticated: suite Invalid value for hash length (bytes): %zu (should be 20 or 128)",
                    raw_packet_.len()-cipher_applicable_length_);
            is_valid_ = false;
            return false;
            break;
    }
    raw_packet_.clear();
    return true;
}
//----------------------------------------------------------------------
int
LTPUDPSegment::Parse_Trailer(u_char * bp, int current_byte, int length)
{
    // Save off the entire buffer and move the cipher stuff to the IsAuthenticated() method
    log_debug("Inside Parse Trailer! current_byte=%d length=%d",current_byte, length);

    int return_result = 0;
   
    bool not_found = true;
    u_int32_t authval_length = 0;

    raw_packet_.clear();

    memcpy(raw_packet_.tail_buf(length),bp,length);
    raw_packet_.incr_len(length);

    while (not_found) { // keep looking if there are multple.
        cipher_applicable_length_ = current_byte;
        if(current_byte >= length)break;
        if (bp[current_byte] == 0x00) { // look for LTPUDP_AUTH byte
            log_debug("Parse_Trailer: Found LTPUDP_AUTH byte"); 
            not_found = false;
            current_byte++;
        }
        int decode_status = SDNV::decode(bp+current_byte, length-current_byte, &authval_length);
        CHECK_DECODE_STATUS
        cipher_trailer_offset_       =  current_byte;
        if(not_found)current_byte    += authval_length;
        if(not_found && current_byte == length)break;
    }

    if(not_found)
    {
        return_result = -1;
    }

    log_debug("Parse_Trailer: cipher_offset=%d cipher_applicable_length=%d return_result=%d",
              cipher_trailer_offset_,cipher_applicable_length_,return_result);

    return return_result;
}
//----------------------------------------------------------------------
int
LTPUDPSegment::suite_result_len(int suite)
{
    int result = 0;
    switch(suite)
    {
        case 0:
            result = 20; // RSA-SHA1-80 Key length
            break;
        case 1:
            result = 128; // RSA-SHA1-256 Key length
            break;
        case 255:
            result = 20; // NULL Ciphersuite Key length
            break;
        default:
            result = -1;  // not supported.
            break;
    }
    return result;
}

#endif
//----------------------------------------------------------------------
void
LTPUDPSegment::Dump_Current_Buffer(char * location)
{
    (void) location; // prevent warning when debug dissabled

    char hold_byte[4];
    char * output_str = (char *) malloc(packet_len_ * 3);
    if(output_str != (char *) NULL)
    {
        memset(output_str,0,packet_len_*3);
        log_debug("%s DCBuffer(%d bytes): ",location,packet_len_);
        for(int counter = 0 ; counter < packet_len_ ; counter++) {
            sprintf(hold_byte,"%02X ",(u_char) *(packet_.buf()+counter));
            strcat(output_str,hold_byte);
        }
        log_debug("%s",output_str);
        free(output_str);
    }
}



//
//  LTPUDPDataSegment.........................
//
//--------------------------------------------------------------------------
LTPUDPDataSegment::LTPUDPDataSegment(u_char * bp, int len)
{
    payload_length_ = 0;
    offset_ = 0;
    start_byte_ = 0;
    stop_byte_ = 0;

    if (NULL != timer_) {
        log_crit("LTPUDPDataSegment(bp, len) constructor - timer_ is not NULL!");
        timer_ = NULL;
    }

    log_debug("Creating DS..");
    if (Parse_Buffer(bp, len) != 0) 
        is_valid_ = false;
}

//--------------------------------------------------------------------------
//LTPUDPDataSegment::LTPUDPDataSegment(LTPUDPDataSegment& from) : LTPUDPSegment(from)
//{
//    //log_debug("Creating DS..");
//    control_flags_     = from.control_flags_;
//    is_checkpoint_     = from.is_checkpoint_;
//    engine_id_         = from.engine_id_;
//    session_id_        = from.session_id_;
//    serial_number_     = from.serial_number_;
//    checkpoint_id_     = from.checkpoint_id_;
//    client_service_id_ = from.client_service_id_;
//    key_               = from.key_;
//    segment_type_      = from.segment_type_;
//    offset_            = from.offset_;
//    start_byte_        = from.start_byte_;
//    packet_len_        = from.packet_len_;
//    payload_length_    = from.payload_length_;
//    packet_            = from.packet_;
//    payload_           = from.payload_;
//}

//--------------------------------------------------------------------------
LTPUDPDataSegment::LTPUDPDataSegment(LTPUDPSession * session) : LTPUDPSegment(session)
{
    //log_debug("Creating DS..");
    segment_type_      = LTPUDPSegment::LTPUDP_SEGMENT_DS;
    is_checkpoint_     = false;
    offset_            = 0;
    start_byte_        = 0;
    stop_byte_         = 0;
    payload_length_    = 0;
    control_flags_     = 0;

    if (NULL != timer_) {
        log_crit("LTPUDPDataSegment(session) constructor - timer_ is not NULL!");
        timer_ = NULL;
    }
}

//--------------------------------------------------------------------------
LTPUDPDataSegment::~LTPUDPDataSegment()
{
    payload_.clear();
}

//--------------------------------------------------------------------------
void LTPUDPDataSegment::Set_Endofblock()
{
    if(IsRed())
    {
        is_redendofblock_   = true;
    } else if(IsGreen()) {
        is_greenendofblock_ = true;
    }
    u_char control_byte = control_flags_;
    if (packet_.len() == 0) {
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    }
    
    control_byte |= 0x03; // add in checkpoint && EOB
    memcpy(packet_.buf(), &control_byte, 1); // set checkpoint flags
    control_flags_ = control_byte;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Unset_Endofblock()
{
    if(IsRed())
    {
        is_redendofblock_    = false;
    } else if(IsGreen()) {
        is_greenendofblock_  = false;
    }
    u_char control_byte = control_flags_;
    if (packet_.len() == 0) {
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    }
    control_byte &= 0x0C; // remove checkpoint
    memset(packet_.buf(), control_byte, 1); // clear buffer 
    control_flags_ = control_byte;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Set_Checkpoint()
{
    is_checkpoint_ = true;
    u_char control_byte = control_flags_;

    if (packet_.len() == 0) {
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    }
    
    control_byte |= (u_char) 0x01; // add in checkpoint  onlhy needs one bit if red!!
    memcpy(packet_.buf(), &control_byte, 1); // set checkpoint flags
    control_flags_ = control_byte;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Set_Segment_Color_Flags()
{
    u_char control_byte = control_flags_;

    if (packet_.len() == 0) {
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    }
    
    if(IsGreen()) { 
        control_byte |= 0x04; //  if green then turn on the exec put
    } else  {
        control_byte &= 0x04; //  don't need this on if red
    }
    memcpy(packet_.buf(), &control_byte, 1); // set checkpoint flags
    control_flags_ = control_byte;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Unset_Checkpoint()
{
    is_checkpoint_ = false;
    u_char control_byte = control_flags_;
    if (packet_.len() == 0) {
        memcpy(packet_.buf(1), &control_byte, 1);
        packet_.incr_len(1);
    }
    control_byte &= 0x0E; // remove checkpoint
    memset(packet_.buf(), control_byte, 1); // set control byte
    control_flags_ = control_byte;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Set_Start_Byte(int start_byte)
{
    start_byte_ = start_byte;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Set_Stop_Byte(int stop_byte)
{
    stop_byte_ = stop_byte;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Set_Length(int len)
{
    payload_length_ = len;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Set_Offset(int offset)
{
    offset_ = offset;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Encode_Buffer()
{
    //log_always("DS EB no args");
    if (packet_.len() > 0) {
        //log_debug("Setting the control_flags since the buffer has size");
        control_flags_ = *packet_.buf();
    //} else {
        // control_flags_ will be set in calls below
    }
    
    //if(IsRed()) {
    //    log_debug("EncodeBuffer Checkpoint: %s EndOfBlock: %s",is_checkpoint_?"true":"false",is_redendofblock_?"true":"false");
    //} else if(IsGreen()) {
    //    log_debug("EncodeBuffer Checkpoint: %s EndOfBlock: %s",is_checkpoint_?"true":"false",is_greenendofblock_?"true":"false");
    //} else {
    //    log_debug("EncodeBuffer Checkpoint:  no valid color for segment");
    //}

    Set_Segment_Color_Flags();

    if (is_checkpoint_) Set_Checkpoint();

    if (is_redendofblock_ || is_greenendofblock_) Set_Endofblock();

    LTPUDPSegment::Create_LTP_Header();

    /*
     *  Client Service ID 
     */ 
    LTPUDPSegment::Encode_Field(client_service_id_);
    
    /*
     *  Offset 
     */ 
    LTPUDPSegment::Encode_Field(offset_);
  
    /*
     *  Length 
     */ 
    log_debug("Inside Encode Buffer (DS)... payload_length_ = %d",payload_length_);

    LTPUDPSegment::Encode_Field(payload_length_);

    if(IsRed()) {

        if(is_checkpoint_)
        {
            LTPUDPSegment::Encode_Field(checkpoint_id_);
            LTPUDPSegment::Encode_Field(serial_number_);
        }
    }
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Encode_Buffer(u_char * data, int length, int start_byte, bool checkpoint)
{
    payload_length_ = length;
    if(IsRed()) {
       is_redendofblock_  = checkpoint;
       is_checkpoint_     = checkpoint;
    } else if(IsGreen()) {
       is_checkpoint_      = false;
       is_greenendofblock_ = checkpoint;
    }
    offset_         = start_byte;
    start_byte_     = start_byte;
    stop_byte_      = start_byte+length-1; 
    //log_debug("Encode_Buffer Start = %d Stop = %d", start_byte_, stop_byte_);

    Encode_Buffer();
    payload_.clear();

    memcpy(payload_.tail_buf(length),data,length);
    payload_.incr_len(length);
    segment_type_   = LTPUDPSegment::LTPUDP_SEGMENT_DS;
    payload_length_ = length;

    Set_Key();

    memcpy(packet_.tail_buf(payload_length_),payload_.buf(),payload_length_);
    packet_.incr_len(payload_length_);
    packet_len_    += payload_length_;

#ifdef LTPUDP_AUTH_ENABLED
    if(is_security_enabled_)
    {
        log_debug("From DS EB calling CT");
        Create_Trailer();
    }
#endif

}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Set_Key()
{
    char simple_key[52] = "";
    sprintf(simple_key,"%20.20d:%20.20d",start_byte_,stop_byte_);
    key_            = simple_key;
}

//-----------------------------------------------------------------
void LTPUDPDataSegment::Encode_All()
{
    log_debug("Encode_ALL .... Control_Byte = %02X",control_flags_);
    packet_.clear();
    packet_len_ = 0;
    Encode_Buffer();
  
    memcpy(packet_.tail_buf(payload_length_),payload_.buf(),payload_length_);
    packet_.incr_len(payload_length_);
    packet_len_ += payload_length_;

#ifdef LTPUDP_AUTH_ENABLED
    if(is_security_enabled_)
    {
        log_debug("From DS EA calling CT");
        Create_Trailer();
    }
#endif
}

//-----------------------------------------------------------------
int LTPUDPDataSegment::Parse_Buffer(u_char * bp, int length)
{
    char temp_chars[30]    = "";
   
    int current_byte = 0;
    int decode_status  = 0;
    
    if(LTPUDPSegment::Parse_Buffer(bp, length, &current_byte) == 0)
    {
        //log_debug("DS Parse_Buffer:After Segment ParseBuffer byte offset = %d",current_byte);
        segment_type_ = LTPUDPSegment::LTPUDP_SEGMENT_DS;

        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &client_service_id_);
        CHECK_DECODE_STATUS
        log_debug("DS Parse_Buffer:client_service_id = %d",client_service_id_);

        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &offset_);
        CHECK_DECODE_STATUS
        log_debug("DS Parse_Buffer:offset_ = %d",offset_);

        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &payload_length_);
        CHECK_DECODE_STATUS
        log_debug("DS Parse_Buffer:payload_length_ = %d",payload_length_);

        start_byte_ = offset_;
        stop_byte_  = offset_+payload_length_-1;

        Set_Key();

        if(IsRed()) {

            if(is_checkpoint_)  // this is a checkpoint segment so it should contain these two entries.
            {
                decode_status = SDNV::decode(bp+current_byte, length-current_byte, &checkpoint_id_);
                CHECK_DECODE_STATUS
                //log_debug("DS Parse_Data checkpoint_ is %"PRIu64,checkpoint_id_);

                decode_status = SDNV::decode(bp+current_byte, length-current_byte, &serial_number_);
                CHECK_DECODE_STATUS
                //log_debug("DS RCVR process_data:Setting Report_Serial Number to %"PRIu64,serial_number_);
                if(serial_number_ != 0)
                {
                    // data_serial has the report_serial being acknowledged.
                    sprintf(temp_chars,"%20.20"PRIu64,serial_number_);
                    serial_number_str_ = temp_chars;
                }
            }
        }
        
        if(payload_length_ > 0) {
            if((int) (current_byte + payload_length_) > (int) length) {
                 log_debug("DS Problem with data portion of packet payload... packet underrun.");
                 is_valid_ = false;
                 return -1;
            }
            memcpy(payload_.buf(payload_length_),bp+current_byte,payload_length_); // only copy the payload
            payload_.incr_len(payload_length_);

        } else {
            log_debug("Problem with data portion of packet payload");
            is_valid_ = false;
        }

        log_debug("PARSE DS: suite:%d key_id:%d engine:%s",cipher_suite_,cipher_key_id_,cipher_engine_.c_str());

#ifdef LTPUDP_AUTH_ENABLED
        if (is_security_enabled_) {
            current_byte += payload_length_; // bump current byte past payload
            Parse_Trailer(bp, current_byte, length); 
        }
#endif
        log_debug("PARSE DS: length:%d payload_length:%d",length, payload_length_);

    } else {
        is_valid_ = false;
    }
    return is_valid_ ? 0 : 1;
}

//
//  LTPUDPReportSegment .................................
//
//----------------------------------------------------------------------
LTPUDPReportSegment::LTPUDPReportSegment(u_char * bp, int len)
{
    //log_debug("Creating RS....");
    if(Parse_Buffer(bp,len) != 0)is_valid_ = false;
}

//----------------------------------------------------------------------
int LTPUDPReportSegment::Parse_Buffer(u_char * bp, int length)
{
    u_int32_t reception_offset = 0;
    u_int32_t reception_length = 0;

    // HMAC context create and decode if security enabled

    int ctr       = 0;  // declare this to make sure all data was there.
    int current_byte = 0;
    int decode_status  = 0;

    if(LTPUDPSegment::Parse_Buffer(bp, length, &current_byte) == 0)
    {
        if(!is_valid_)return -1;

        segment_type_ = LTPUDPSegment::LTPUDP_SEGMENT_RS; 

        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &report_serial_number_);
        CHECK_DECODE_STATUS
        log_debug("RS : report serial number = %"PRIu64,report_serial_number_);
        Set_Key();

        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &checkpoint_id_);
        CHECK_DECODE_STATUS
        log_debug("RS : checkpoint id = %"PRIu64,checkpoint_id_);
        
        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &upper_bounds_);
        CHECK_DECODE_STATUS
        log_debug("RS : upper = %d",upper_bounds_);
        
        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &lower_bounds_);
        CHECK_DECODE_STATUS
        log_debug("RS : lower = %d",lower_bounds_);
        
        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &claims_);
        CHECK_DECODE_STATUS
        log_debug("RS : claims = %d",claims_);

        for(ctr = 0 ; ctr < (int) claims_ ; ctr++) {// step through the claims

            decode_status = SDNV::decode(bp+current_byte, length-current_byte, &reception_offset);
            CHECK_DECODE_STATUS
            log_debug("RS : %d offset = %d",ctr,reception_offset);
            
            decode_status = SDNV::decode(bp+current_byte, length-current_byte, &reception_length);
            CHECK_DECODE_STATUS
            log_debug("RS : %d offset = %d",ctr,reception_offset);

            LTPUDPReportClaim * rc = new LTPUDPReportClaim(reception_offset,reception_length);
            claim_map_.insert(std::pair<std::string, LTPUDPReportClaim*>(rc->Key(), rc)); 
        }

        if(ctr == (int) claims_)is_valid_ = true;

        if(current_byte > length)
        {
            log_debug("We've gone past the end of the report segment...");
            is_valid_ = false;
        }
    } else {
        is_valid_ = false;
    }

#ifdef LTPUDP_AUTH_ENABLED
    if (is_security_enabled_) {
        Parse_Trailer(bp, current_byte, length); 
    }
#endif

    return is_valid_ ? 0 : 1;
}

//----------------------------------------------------------------------
void 
LTPUDPReportSegment::Set_Key()
{
    char temp[25] = "";
    sprintf(temp,"%20.20"PRIu64,report_serial_number_);
    key_ = temp;
}

//----------------------------------------------------------------------
void
LTPUDPReportSegment::Encode_All( )
{
    u_int32_t temp_int;
    RC_MAP::iterator loop_iterator;

    LTPUDPReportClaim * rc = (LTPUDPReportClaim *) NULL;

    packet_.clear();

    LTPUDPSegment::Create_LTP_Header();         

    Encode_Counters();
        
    for(loop_iterator = claim_map_.begin(); loop_iterator != claim_map_.end(); loop_iterator++) 
    {
        rc = loop_iterator->second;
        temp_int = (u_int32_t) rc->Offset();
        LTPUDPSegment::Encode_Field(temp_int);

        temp_int = (u_int32_t) rc->Length();
        LTPUDPSegment::Encode_Field(temp_int);
    }
#ifdef LTPUDP_AUTH_ENABLED
    if(is_security_enabled_)
    {
        log_debug("From RS EA calling CT");
        Create_Trailer();
    }
#endif
}

//----------------------------------------------------------------------
LTPUDPReportSegment::LTPUDPReportSegment(LTPUDPSession * session, int red_green) : LTPUDPSegment(session)
{
    if(session->Report_Serial_Number() == 0)
    {
       session->Set_Report_Serial_Number(LTPUDP_14BIT_RAND);
    } else {
       session->Set_Report_Serial_Number(session->Report_Serial_Number()+1);
    }

    checkpoint_id_            = session->Checkpoint_ID();
    parent_                   = session->Parent();

    control_flags_ = 0x08; // this is a ReportSegment

    cipher_suite_  = session->Outbound_Cipher_Suite();
    cipher_key_id_ = session->Outbound_Cipher_Key_Id();
    cipher_engine_ = session->Outbound_Cipher_Engine();
    is_security_enabled_ = ((int) cipher_suite_ != -1) ? true : false;

    LTPUDPSegment::Create_LTP_Header();

    segment_type_ = LTPUDPSegment::LTPUDP_SEGMENT_RS; 

    memset(packet_.buf(),0x08,1);

    report_serial_number_ = session->Report_Serial_Number();

    Set_Key();

    timer_                    = 0;
    retrans_retries_          = 0;
    last_packet_time_ = parent_->AOS_Counter();
    Build_RS_Data(session, red_green); 
}

//----------------------------------------------------------------------
LTPUDPReportSegment::LTPUDPReportSegment()
{
    report_serial_number_    = 0;
    timer_            = 0;
    retrans_retries_  = 0;
    last_packet_time_ = 0;
    parent_           = 0;
}

//----------------------------------------------------------------------
LTPUDPReportSegment::LTPUDPReportSegment(LTPUDPReportSegment& from) : LTPUDPSegment(from)
{
    report_serial_number_    = from.report_serial_number_;

    Set_Key();

    last_packet_time_ = from.last_packet_time_;
    parent_           = from.parent_;
    retrans_retries_  = from.retrans_retries_;
    claims_           = from.claims_;
}

//----------------------------------------------------------------------
LTPUDPReportSegment::~LTPUDPReportSegment()
{
    RC_MAP::iterator seg_iter = claim_map_.begin();

    while(seg_iter != claim_map_.end()) {
        LTPUDPReportClaim * segptr = seg_iter->second; 
        delete segptr; 
        claim_map_.erase(seg_iter->first);
        seg_iter = claim_map_.begin();
    }
}

//----------------------------------------------------------------------
void
LTPUDPReportSegment::Build_RS_Data(LTPUDPSession * session, int red_green)
{                                                
    bool      consecutive      = false;

    u_int32_t claim_start      = 0;
    u_int32_t claim_length     = 0;
    u_int32_t next_claim_start = 0;

    LTPUDPDataSegment * this_segment = (LTPUDPDataSegment *) NULL;
    LTPUDPReportClaim * rc           = (LTPUDPReportClaim *) NULL;
 
    DS_MAP::iterator loop_iterator;
 
    DS_MAP * segments = (DS_MAP *) NULL;
 
    // loop through finding the min and max and counting the number of segments
 
    if(red_green == RED_SEGMENT) {
       segments = &session->Red_Segments();
    } else {
       segments = &session->Green_Segments();
    }  

    claims_          = 0; // clear all in case map is empty
    upper_bounds_    = 0;
    lower_bounds_    = 0; 

    if(!segments->empty())
    {
        claims_ = segments->size();

        loop_iterator = segments->begin();
        this_segment  = loop_iterator->second;
        loop_iterator = --segments->end();
        if(loop_iterator != segments->end())
        {
            this_segment  = loop_iterator->second;
            upper_bounds_ = this_segment->Offset() + this_segment->Payload_Length();
        }
    }

    // load all the segments

    for(loop_iterator = segments->begin(); loop_iterator != segments->end(); loop_iterator++)
    {
        this_segment  = loop_iterator->second;

        consecutive = (next_claim_start == this_segment->Offset());

        if(loop_iterator == segments->begin()) { // set the first claim...
            claim_start      = this_segment->Offset();
            claim_length     = this_segment->Payload_Length();
            next_claim_start = claim_start + claim_length;
        } else if (consecutive) {
            claim_length    += this_segment->Payload_Length();
            next_claim_start = this_segment->Offset() + this_segment->Payload_Length();
            if(claim_start != this_segment->Offset())claims_--; // subtract consecutive segment claim
        } else {
            rc               = new LTPUDPReportClaim(claim_start,claim_length);
            claim_map_.insert(std::pair<std::string, LTPUDPReportClaim*>(rc->Key(), rc)); 
            claim_start      = this_segment->Offset();
            claim_length     = this_segment->Payload_Length();
            next_claim_start = claim_start + claim_length;
        }
    }   

    // insert the last one!!!
    rc = new LTPUDPReportClaim(claim_start,claim_length);
    claim_map_.insert(std::pair<std::string, LTPUDPReportClaim*>(rc->Key(), rc)); 

    Encode_All();

    return;
 
}

//--------------------------------------------------------------------------
void 
LTPUDPReportSegment::Encode_Counters()
{
    LTPUDPSegment::Encode_Field(report_serial_number_);
    LTPUDPSegment::Encode_Field(checkpoint_id_);
    LTPUDPSegment::Encode_Field(upper_bounds_);
    LTPUDPSegment::Encode_Field(lower_bounds_);
    LTPUDPSegment::Encode_Field(claims_);
}

//
//  LTPUDPReportAckSegment .................................
//
//----------------------------------------------------------------------    
LTPUDPReportAckSegment::LTPUDPReportAckSegment(u_char * bp, int len)
{
    if(Parse_Buffer(bp,len) != 0)is_valid_ = false;
}

//--------------------------------------------------------------------------
int LTPUDPReportAckSegment::Parse_Buffer(u_char * bp, int length)
{
    int current_byte = 0;
    int decode_status  = 0;
    // HMAC context create and decode if security enabled

    if(LTPUDPSegment::Parse_Buffer(bp, length, &current_byte) == 0)
    {
        if(!is_valid_)return -1;

        segment_type_ = LTPUDPSegment::LTPUDP_SEGMENT_RAS; 

        decode_status = SDNV::decode(bp+current_byte, length-current_byte, &report_serial_number_);

        CHECK_DECODE_STATUS

        if(current_byte > length)
        {
            log_debug("We've gone past the end of the report ack segment...");
            is_valid_ = false;
        }
#ifdef LTPUDP_AUTH_ENABLED
        if (is_security_enabled_) {
            Parse_Trailer(bp, current_byte, length); 
        }
#endif

    } else {
        return -1;
    }
    return is_valid_ ? 0 : 1;
}

//--------------------------------------------------------------------------
LTPUDPReportAckSegment::LTPUDPReportAckSegment(LTPUDPReportSegment * rs, LTPUDPSession * session) : LTPUDPSegment(*rs)
{
    // HMAC context create and decode if security enabled
    control_flags_   = 0x09;
    serial_number_   = rs->Serial_ID();
    parent_          = rs->Parent();
    log_debug("ReportAck c'tor");
    cipher_suite_  = session->Outbound_Cipher_Suite();
    cipher_key_id_ = session->Outbound_Cipher_Key_Id();
    cipher_engine_ = session->Outbound_Cipher_Engine();
    is_security_enabled_ = ((int) cipher_suite_ != -1) ? true : false;
    LTPUDPSegment::Create_LTP_Header();
    report_serial_number_ = rs->Report_Serial_Number();

    segment_type_         = LTPUDPSegment::LTPUDP_SEGMENT_RAS;
    Encode_Field(report_serial_number_);
    memset(packet_.buf(),0x09,1);
#ifdef LTPUDP_AUTH_ENABLED
    if(is_security_enabled_)
    {
        log_debug("From RAS Constructor from RS calling CT");
        Create_Trailer();
    }
#endif
} 

//--------------------------------------------------------------------------
LTPUDPReportAckSegment::LTPUDPReportAckSegment(LTPUDPSession * session, LTPUDPCallbackIF* parent) : LTPUDPSegment(session)
{
    report_serial_number_ = session->Report_Serial_Number();
    segment_type_         = LTPUDPSegment::LTPUDP_SEGMENT_RAS;
    parent_               = parent;

    last_packet_time_ = parent_->AOS_Counter();
    log_debug("ReportAck c'tor 2");
    cipher_suite_  = session->Outbound_Cipher_Suite();
    cipher_key_id_ = session->Outbound_Cipher_Key_Id();
    cipher_engine_ = session->Outbound_Cipher_Engine();
    is_security_enabled_ = ((int) cipher_suite_ != -1) ? true : false;

    LTPUDPSegment::Create_LTP_Header();
    LTPUDPSegment::Encode_Field(report_serial_number_);
    memset(packet_.buf(),0x09,1);
#ifdef LTPUDP_AUTH_ENABLED
    if(is_security_enabled_)
    {
        log_debug("From RAS Constructor from session  calling CT");
        Create_Trailer();
    }
#endif
}

//--------------------------------------------------------------------------
LTPUDPReportAckSegment::LTPUDPReportAckSegment()
{
    report_serial_number_     = 0;
    last_packet_time_         = 0;
    parent_                   = 0;
}

//--------------------------------------------------------------------------
LTPUDPReportAckSegment::LTPUDPReportAckSegment(LTPUDPReportAckSegment& from) : LTPUDPSegment(from)
{
    report_serial_number_     = from.report_serial_number_;
    last_packet_time_         = from.last_packet_time_;
    parent_                   = from.parent_;
    retransmit_ctr_           = from.retransmit_ctr_;
}

//--------------------------------------------------------------------------
LTPUDPReportAckSegment::~LTPUDPReportAckSegment()
{
}

//--------------------------------------------------------------------------
time_t 
LTPUDPReportAckSegment::Last_Packet_Time()
{
    return last_packet_time_;
}


//
//  LTPUDPCancelSegment .................................
//
//----------------------------------------------------------------------    
LTPUDPCancelSegment::LTPUDPCancelSegment(u_char * bp, int len, int segment_type)
{
    if(Parse_Buffer(bp,len, segment_type) != 0)is_valid_ = false;
}

//-----------------------------------------------------------------
LTPUDPCancelSegment::LTPUDPCancelSegment(LTPUDPSession * session, int segment_type, u_char reason_code, LTPUDPCallbackIF * parent) : LTPUDPSegment(session)
{
    cipher_suite_        = session->Outbound_Cipher_Suite();
    cipher_key_id_       = session->Outbound_Cipher_Key_Id();
    cipher_engine_       = session->Outbound_Cipher_Engine();
    is_security_enabled_ = ((int) cipher_suite_ != -1) ? true : false;

    LTPUDPSegment::Create_LTP_Header();

    memset(packet_.tail_buf(1),reason_code,1);

    packet_.incr_len(1);

    switch(segment_type)
    {
        case LTPUDPSegment::LTPUDP_SEGMENT_CS_BS:
            memset(packet_.buf(),0x0C,1); // 12 Cancel Segment from Block Sender
            break;

        case LTPUDPSegment::LTPUDP_SEGMENT_CS_BR:
            memset(packet_.buf(),0x0E,1); // 14 Cancel segment from Block Receiver
            break;
   
        default:
            memset(packet_.buf(),0x0C,1); //  just make it a valid one but complain
            log_debug("Unknwon segment type.... ");
            break;

    }

    reason_code_    = reason_code;
    segment_type_   = segment_type;
    parent_         = parent;
    packet_len_++;

#ifdef LTPUDP_AUTH_ENABLED
    if(is_security_enabled_)
    {
        log_debug("From CS Constructor from session  calling CT");
        Create_Trailer();
    }
#endif
} 

//-----------------------------------------------------------------
int LTPUDPCancelSegment::Parse_Buffer(u_char * bp, int length, int segment_type)
{
    int current_byte = 0;
    // HMAC context create and decode if security enabled

    if(LTPUDPSegment::Parse_Buffer(bp, length, &current_byte) == 0)
    {       
        if(!is_valid_)return -1;

        segment_type_ = segment_type; 
        reason_code_  = *(bp+current_byte);
        current_byte += 1;
    } else {
        return -1;
    }

    if(current_byte > length)
    {
        log_debug("We've gone past the end of the cancel segment...");
        is_valid_ = false;
        return -1;
    }

#ifdef LTPUDP_AUTH_ENABLED
    if (is_security_enabled_) {
        Parse_Trailer(bp, current_byte, length); 
    }
#endif

    return 0;
}

//-----------------------------------------------------------------
LTPUDPCancelSegment::LTPUDPCancelSegment()
{
    reason_code_  = 0x00;
    packet_len_   = 0;
    parent_       = 0;
}

//-----------------------------------------------------------------
LTPUDPCancelSegment::LTPUDPCancelSegment(LTPUDPCancelSegment& from)  : LTPUDPSegment(from)
{
    reason_code_  = from.reason_code_;
    packet_len_   = from.packet_len_;
    packet_       = from.packet_;
    parent_       = from.parent_;

}

//-----------------------------------------------------------------
LTPUDPCancelSegment::~LTPUDPCancelSegment()
{
}


//
//  LTPUDPCancelAckSegment .................................
//
//----------------------------------------------------------------------    
 LTPUDPCancelAckSegment::LTPUDPCancelAckSegment(u_char * bp, int len, int segment_type)
{
    if(Parse_Buffer(bp,len, segment_type) != 0)is_valid_ = false;
}

//-----------------------------------------------------------------
int LTPUDPCancelAckSegment::Parse_Buffer(u_char * bp, int length, int segment_type)
{
    int  current_byte = 0;

    if(LTPUDPSegment::Parse_Buffer(bp, length, &current_byte) != 0)is_valid_ = false;

    if(!is_valid_)return -1;

    segment_type_ = segment_type;

#ifdef LTPUDP_AUTH_ENABLED
    if (is_security_enabled_) {
        Parse_Trailer(bp, current_byte, length); 
    }
#endif

    return 0;
}

//-----------------------------------------------------------------
LTPUDPCancelAckSegment::LTPUDPCancelAckSegment()
{
    packet_len_   = 0;
    parent_       = 0;
}

//-----------------------------------------------------------------
LTPUDPCancelAckSegment::LTPUDPCancelAckSegment(LTPUDPCancelSegment * from, u_char control_byte, LTPUDPSession * session)
{
    key_ = "";   //  <string version of offset and length used as key in segments map>
 
    packet_len_          = 0;
    engine_id_           = from->Engine_ID();
    session_id_          = from->Session_ID();;
    parent_              = from->Parent();
    control_flags_       = control_byte;
    log_debug("CS c'tor");
    is_security_enabled_ = session->IsSecurityEnabled();
    cipher_suite_        = session->Outbound_Cipher_Suite();
    cipher_key_id_       = session->Outbound_Cipher_Key_Id();
    cipher_engine_       = session->Outbound_Cipher_Engine();
    is_security_enabled_ = ((int) cipher_suite_ != -1) ? true : false;

    LTPUDPSegment::Create_LTP_Header();
    if(control_byte == LTPUDPSEGMENT_CAS_BR_BYTE)
        segment_type_      = LTPUDPSegment::LTPUDP_SEGMENT_CAS_BR;
    else
        segment_type_      = LTPUDPSegment::LTPUDP_SEGMENT_CAS_BS;
#ifdef LTPUDP_AUTH_ENABLED
    if(is_security_enabled_)
    {
        log_debug("From CAS Constructor from segment calling CT");
        Create_Trailer();
    }
#endif
}

//-----------------------------------------------------------------
LTPUDPCancelAckSegment::LTPUDPCancelAckSegment(LTPUDPCancelAckSegment& from)  : LTPUDPSegment(from)
{
    packet_len_   = from.packet_len_;
    parent_       = from.parent_;
}

//-----------------------------------------------------------------
LTPUDPCancelAckSegment::~LTPUDPCancelAckSegment()
{
}

//-----------------------------------------------------------------
const char * LTPUDPCancelSegment::Get_Reason_Code(int reason_code)
{
    const char * return_var = "";
    static char temp[30];
    switch(reason_code)
    {
        case LTPUDPCancelSegment::LTPUDP_CS_REASON_USR_CANCLED:
            return_var = "Client Service Cancelled session";
            break;
        case LTPUDPCancelSegment::LTPUDP_CS_REASON_UNREACHABLE:
            return_var = "Unreashable client service";
            break;
        case LTPUDPCancelSegment::LTPUDP_CS_REASON_RLEXC:
            return_var = "Retransmission limit exceeded";
            break;
        case LTPUDPCancelSegment::LTPUDP_CS_REASON_MISCOLORED:
            return_var = "Miscolored segment";
            break;
        case LTPUDPCancelSegment::LTPUDP_CS_REASON_SYS_CNCLD:
            return_var = "A system error condition cuased Unexpected session termination";
            break;
        case LTPUDPCancelSegment::LTPUDP_CS_REASON_RXMTCYCEX:
            return_var = "Exceede the retransmission cycles limit";
            break;
        default:
            sprintf(temp,"ReasonCode Reserved (%02X)",(reason_code & 0xFF));
            return_var = temp;
    }
    return return_var;
}


//
//  LTPUDPReportClaims .................................
//
//----------------------------------------------------------------------    
LTPUDPReportClaim::LTPUDPReportClaim(int offset, int len)
{
    char temp[24];
    sprintf(temp,"%10.10d:%10.10d",offset,len);
    offset_      = offset;
    length_      = len;
    key_         = temp;
}

//-----------------------------------------------------------------
LTPUDPReportClaim::LTPUDPReportClaim()
{
    length_       = 0;
    offset_       = 0;
}

//-----------------------------------------------------------------
LTPUDPReportClaim::LTPUDPReportClaim(LTPUDPReportClaim& from)
{
    length_       = from.length_;
    offset_       = from.offset_;
}

//-----------------------------------------------------------------
LTPUDPReportClaim::~LTPUDPReportClaim()
{

}
}


#endif // LTPUDP_ENABLED
