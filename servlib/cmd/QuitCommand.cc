
#include "QuitCommand.h"

QuitCommand QuitCommand::instance_;

QuitCommand::QuitCommand() : AutoCommandModule("quit") {}

int
QuitCommand::exec(int argc, const char** argv, Tcl_Interp* interp)
{
    printf("Does exit shut everything down cleanly?");

    return TCL_OK;
}

const char*
QuitCommand::help_string()
{
    return("not sure if this is needed; 'exit' seems to quit (cleanly?)");
}

