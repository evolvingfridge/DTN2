test::name ipc-version-test
net::num_nodes 1

manifest::file apps/dtntest/dtntest dtntest

dtn::config

set version 54321
foreach {var val} $opt(opts) {
    if {$var == "-version" || $var == "version"} {
        set version $val
    }
}

test::script {
    puts "* Running dtnd and dtntest"
    dtn::run_dtnd 0
    dtn::run_dtntest 0

    puts "* Waiting for dtnd and dtntest to start up"
    dtn::wait_for_dtnd 0
    dtn::wait_for_dtntest 0

    puts "* Doing a dtn_open with default version (should work)"
    set id [dtn::tell_dtntest 0 dtn_open]
    puts "* dtn_open succeeded"
    dtn::tell_dtntest 0 dtn_close $id
    puts "* dtn_close succeeded"

    puts "* Setting up flamebox-ignore"
    dtn::tell_dtnd 0 log /test always \
	"flamebox-ignore ign1 handshake .*s version \[0-9\]* != DTN_IPC_VERSION"
    
    puts "* Doing a dtn_open with version=$version (should fail)"
    if { [catch \
	  {set id [dtn::tell_dtntest 0 dtn_open version=$version]} \
	  errorstr] } {
	puts "  and it did!"
    } else {
	dtn::tell_dtntest 0 dtn_close $id
	error "dtn_open did not fail as expected despite IPC version mismatch"
    }
    
    puts "* Test success!"
}

test::exit_script {
    puts "* clearing flamebox ignores"
    tell_dtnd 0 log /test always "flamebox-ignore-cancel ign1"
    
    puts "* Stopping dtnd and dtntest"
    dtn::stop_dtnd 0
    dtn::stop_dtntest 0
}