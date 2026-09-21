# Applied to GLFW's source tree (the working directory) after it is fetched.
#
# Under XIM, GLFW reports a key press as soon as it is seen, even when the
# input method consumes it (Enter confirming a conversion, Backspace editing
# the composition, Space converting). The app then reacts to keys that were
# meant for the IME. The input method hands every key it does not consume
# back as an unfiltered event, so reporting only those events is enough: the
# IME-consumed presses disappear and the others arrive one round trip later.
set(file "src/x11_window.c")
file(READ "${file}" text)
set(old "if (diff == event->xkey.time || (diff > 0 && diff < ((Time)1 << 31)))")
set(new "if (!filtered && (diff == event->xkey.time || (diff > 0 && diff < ((Time)1 << 31))))")
string(FIND "${text}" "${new}" already)
if(NOT already EQUAL -1)
  return()
endif()
string(FIND "${text}" "${old}" found)
if(found EQUAL -1)
  message(FATAL_ERROR "PatchGlfwXim: the expected line was not found in ${file}; "
                      "GLFW changed, so this patch needs updating.")
endif()
string(REPLACE "${old}" "${new}" text "${text}")
file(WRITE "${file}" "${text}")
