#
#    Copyright 2004-2006 Intel Corporation
# 
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
# 
#        http://www.apache.org/licenses/LICENSE-2.0
# 
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

test::name dtnsim-basic
net::default_num_nodes 1

manifest::file sim/dtnsim dtnsim
manifest::file sim/send-one-bundle.conf send-one-bundle.conf

test::script {
    puts "* Running simulator"
    set pid [run::run 0 "dtnsim" "-c send-one-bundle.conf" "" "" ""]
    run::wait_for_pid_exit 0 $pid
    puts "* Test success!"
}