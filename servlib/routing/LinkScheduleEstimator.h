#include <vector>
#include <oasys/util/IntUtils.h>
#include <oasys/debug/Log.h>

#define CONTACT(s, d) { s, d }
#define absdiff(x,y) ((x<y)?((y)-(x)):((x)-(y)))

#define WARPING_WINDOW .02
#define PERIOD_TOLERANCE .02
#define MAX_DIST 1<<30

namespace dtn {
    
class LinkScheduleEstimator : public oasys::Logger {
public:
    typedef struct {
      unsigned int start;
      unsigned int duration;
    } LogEntry;
    
    typedef std::vector<LogEntry> Log;
    
    static Log* find_schedule(Log* log);

    LinkScheduleEstimator();
private:
    unsigned int entry_dist(Log &a, unsigned int a_index, unsigned int a_offset,
			    Log &b, unsigned int b_index, unsigned int b_offset,
			    unsigned int warping_window);

    unsigned int log_dist_r(Log &a, unsigned int a_index, unsigned int a_offset,
			    Log &b, unsigned int b_index, unsigned int b_offset,
			    unsigned int warping_window);
    
       
    unsigned int log_dist(Log &a, unsigned int a_offset,
		 Log &b, unsigned int b_offset,
		 unsigned int warping_window, int print_table);
    
    unsigned int autocorrelation(Log &log, unsigned int phase, int print_table);    
    void print_log(Log &log, int relative_dates);
    
    Log* generate_samples(Log &schedule,
			  unsigned int log_size,
			  unsigned int start_jitter,
			  double duration_jitter);
    
    unsigned int estimate_period(Log &log);       
    unsigned int seek_to_before_date(Log &log, unsigned int date);                
    unsigned int closest_entry_to_date(Log &log, unsigned int date);
    Log* clone_subsequence(Log &log, unsigned int start, unsigned int len);
        
    unsigned int badness_of_match(Log &pattern,
				  Log &log,
				  unsigned int warping_window, 
                                  unsigned int period);
    
    Log* extract_schedule(Log &log, unsigned int period_estimate);    
    unsigned int refine_period(Log &log, unsigned int period_estimate);
    Log* find_schedule(Log &log);    
};


}