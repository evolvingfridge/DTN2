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


#include <errno.h>
#include <string>
#include <sys/time.h>

#include <oasys/debug/FatalSignals.h>
#include <oasys/debug/Log.h>
#include <oasys/io/NetUtils.h>
#include <oasys/memory/Memory.h>
#include <oasys/tclcmd/ConsoleCommand.h>
#include <oasys/tclcmd/TclCommand.h>
#include <oasys/thread/Timer.h>
#include <oasys/util/Daemonizer.h>
#include <oasys/util/Getopt.h>
#include <oasys/util/Random.h>
#include <oasys/util/StringBuffer.h>

#include "applib/APIServer.h"
#include "cmd/TestCommand.h"
#include "servlib/DTNServer.h"
#include "storage/DTNStorageConfig.h"

extern const char* dtn_version;

/**
 * Namespace for the dtn daemon source code.
 */
namespace dtn {

/**
 * Thin class that implements the daemon itself.
 */
class DTND {
public:
    DTND();
    int main(int argc, char* argv[]);

protected:
    bool                  daemonize_;
    oasys::Daemonizer     daemonizer_;
    int                   random_seed_;
    bool                  random_seed_set_;
    std::string           conf_file_;
    bool                  conf_file_set_;
    bool                  print_version_;
    std::string           loglevelstr_;
    oasys::log_level_t    loglevel_;
    std::string           logfile_;
    TestCommand*          testcmd_;
    oasys::ConsoleCommand* consolecmd_;
    DTNStorageConfig      storage_config_;

    void get_options(int argc, char* argv[]);
    void notify_and_exit(char status);
    void seed_random();
    void init_log();
    void init_testcmd(int argc, char* argv[]);
    void run_console();
};

//----------------------------------------------------------------------
DTND::DTND()
    : daemonize_(false),
      random_seed_(0),
      random_seed_set_(false),
      conf_file_(""),
      conf_file_set_(false),
      print_version_(false),
      loglevelstr_(""),
      loglevel_(LOG_DEFAULT_THRESHOLD),
      logfile_("-"),
      testcmd_(NULL),
      consolecmd_(NULL),
      storage_config_("storage",	// command name
                      "berkeleydb",	// storage type
                      "DTN",		// DB name
                      "/var/dtn/db")	// DB directory
{
    // override defaults from oasys storage config
    storage_config_.db_max_tx_ = 1000;

    testcmd_    = new TestCommand();
    consolecmd_ = new oasys::ConsoleCommand("dtn% ");
}

//----------------------------------------------------------------------
void
DTND::get_options(int argc, char* argv[])
{
    
    // Register all command line options
    oasys::Getopt opts;
    opts.addopt(
        new oasys::BoolOpt('v', "version", &print_version_,
                           "print version information and exit"));

    opts.addopt(
        new oasys::StringOpt('o', "output", &logfile_, "<output>",
                             "file name for logging output "
                             "(default - indicates stdout)"));

    opts.addopt(
        new oasys::StringOpt('l', NULL, &loglevelstr_, "<level>",
                             "default log level [debug|warn|info|crit]"));

    opts.addopt(
        new oasys::StringOpt('c', "conf", &conf_file_, "<conf>",
                             "set the configuration file", &conf_file_set_));
    opts.addopt(
        new oasys::BoolOpt('d', "daemonize", &daemonize_,
                           "run as a daemon"));

    opts.addopt(
        new oasys::BoolOpt('t', "tidy", &storage_config_.tidy_,
                           "clear database and initialize tables on startup"));

    opts.addopt(
        new oasys::BoolOpt(0, "init-db", &storage_config_.init_,
                           "initialize database on startup"));

    opts.addopt(
        new oasys::IntOpt('s', "seed", &random_seed_, "<seed>",
                          "random number generator seed", &random_seed_set_));

    opts.addopt(
        new oasys::InAddrOpt(0, "console-addr", &consolecmd_->addr_, "<addr>",
                             "set the console listening addr (default off)"));
    
    opts.addopt(
        new oasys::UInt16Opt(0, "console-port", &consolecmd_->port_, "<port>",
                             "set the console listening port (default off)"));
    
    opts.addopt(
        new oasys::IntOpt('i', 0, &testcmd_->id_, "<id>",
                          "set the test id"));
    
    int remainder = opts.getopt(argv[0], argc, argv);
    if (remainder != argc) 
    {
        fprintf(stderr, "invalid argument '%s'\n", argv[remainder]);
        opts.usage("dtnd");
        exit(1);
    }
}

//----------------------------------------------------------------------
void
DTND::notify_and_exit(char status)
{
    if (daemonize_) {
        daemonizer_.notify_parent(status);
    }
    exit(status);
}

//----------------------------------------------------------------------
void
DTND::seed_random()
{
    // seed the random number generator
    if (!random_seed_set_) 
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        random_seed_ = tv.tv_usec;
    }
    
    log_notice_p("/dtnd", "random seed is %u\n", random_seed_);
    oasys::Random::seed(random_seed_);
}

//----------------------------------------------------------------------
void
DTND::init_log()
{
    // Parse the debugging level argument
    if (loglevelstr_.length() == 0) 
    {
        loglevel_ = oasys::LOG_NOTICE;
    }
    else 
    {
        loglevel_ = oasys::str2level(loglevelstr_.c_str());
        if (loglevel_ == oasys::LOG_INVALID) 
        {
            fprintf(stderr, "invalid level value '%s' for -l option, "
                    "expected debug | info | warning | error | crit\n",
                    loglevelstr_.c_str());
            notify_and_exit(1);
        }
    }
    oasys::Log::init(logfile_.c_str(), loglevel_, "", "~/.dtndebug");
    oasys::Log::instance()->add_reparse_handler(SIGHUP);
    oasys::Log::instance()->add_rotate_handler(SIGUSR1);

    if (daemonize_) {
        if (logfile_ == "-") {
            fprintf(stderr, "daemon mode requires setting of -o <logfile>\n");
            notify_and_exit(1);
        }
        
        oasys::Log::instance()->redirect_stdio();
    }
}

//----------------------------------------------------------------------
void
DTND::init_testcmd(int argc, char* argv[])
{
    for (int i = 0; i < argc; ++i) {
        testcmd_->argv_.append(argv[i]);
        testcmd_->argv_.append(" ");
    }

    testcmd_->bind_vars();
    oasys::TclCommandInterp::instance()->reg(testcmd_);
}

//----------------------------------------------------------------------
void
DTND::run_console()
{
    // launch the console server
    if (consolecmd_->port_ != 0) {
        log_info_p("/dtnd", "starting console on %s:%d",
                   intoa(consolecmd_->addr_), consolecmd_->port_);
        
        oasys::TclCommandInterp::instance()->
            command_server(consolecmd_->prompt_.c_str(),
                           consolecmd_->addr_, consolecmd_->port_);
    }
    
    if (daemonize_ || (consolecmd_->stdio_ == false)) {
        oasys::TclCommandInterp::instance()->event_loop();
    } else {
        oasys::TclCommandInterp::instance()->
            command_loop(consolecmd_->prompt_.c_str());
    }
}

//----------------------------------------------------------------------
int
DTND::main(int argc, char* argv[])
{
#ifdef OASYS_DEBUG_MEMORY_ENABLED
    oasys::DbgMemInfo::init();
#endif

    get_options(argc, argv);

    if (print_version_) 
    {
        printf("%s\n", dtn_version);
        exit(0);
    }

    if (daemonize_) {
        daemonizer_.daemonize(true);
    }

    init_log();
    
    log_notice_p("/dtnd", "DTN daemon starting up... (pid %d)", getpid());
    oasys::FatalSignals::init("dtnd");

    if (oasys::TclCommandInterp::init(argv[0]) != 0)
    {
        log_crit_p("/dtnd", "Can't init TCL");
        notify_and_exit(1);
    }

    seed_random();

    // stop thread creation b/c of initialization dependencies
    oasys::Thread::activate_start_barrier();

    DTNServer* dtnserver = new DTNServer("/dtnd", &storage_config_);
    APIServer* apiserver = new APIServer();

    dtnserver->init();

    oasys::TclCommandInterp::instance()->reg(consolecmd_);
    init_testcmd(argc, argv);

    if (! dtnserver->parse_conf_file(conf_file_, conf_file_set_)) {
        log_err_p("/dtnd", "error in configuration file, exiting...");
        notify_and_exit(1);
    }

    if (storage_config_.init_)
    {
        log_notice_p("/dtnd", "initializing persistent data store");
    }

    if (! dtnserver->init_datastore()) {
        log_err_p("/dtnd", "error initializing data store, exiting...");
        notify_and_exit(1);
    }
    
    // If we're running as --init-db, make an empty database and exit
    if (storage_config_.init_ && !storage_config_.tidy_)
    {
        dtnserver->close_datastore();
        log_info_p("/dtnd", "database initialization complete.");
        notify_and_exit(0);
    }

    // if we've daemonized, now is the time to notify our parent
    // process that we've successfully initialized
    if (daemonize_) {
        daemonizer_.notify_parent(0);
    }
    
    dtnserver->start();
    if (apiserver->enabled()) {
        apiserver->bind_listen_start(apiserver->local_addr(), 
                                     apiserver->local_port());
    }
    oasys::Thread::release_start_barrier(); // run blocked threads

    // if the test script specified something to run for the test,
    // then execute it now
    if (testcmd_->initscript_.length() != 0) {
        oasys::TclCommandInterp::instance()->
            exec_command(testcmd_->initscript_.c_str());
    }

    // allow startup messages to be flushed to standard-out before
    // the prompt is displayed
    oasys::Thread::yield();
    usleep(500000);
    
    run_console();

    log_notice_p("/dtnd", "command loop exited... shutting down daemon");
    oasys::TclCommandInterp::shutdown();
    dtnserver->shutdown();
    
    // close out servers
    delete dtnserver;
    delete apiserver;
    
    // give other threads (like logging) a moment to catch up before exit
    oasys::Thread::yield();
    sleep(1);
    
    // kill logging
    oasys::Log::shutdown();
    
    return 0;
}

} // namespace dtn

int
main(int argc, char* argv[])
{
    dtn::DTND dtnd;
    dtnd.main(argc, argv);
}
