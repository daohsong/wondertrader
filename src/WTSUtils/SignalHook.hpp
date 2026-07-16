#pragma once

#include "./StackTracer/StackTracer.h"

void handle_signal(int signum);
void install_signal_hooks(TracerLogCallback cbLog, ExitHandler sigHandler = ExitHandler{});
