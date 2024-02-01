# CMake generated Testfile for 
# Source directory: /home/debian/microphone/picovoice/demo/c/pvrecorder
# Build directory: /home/debian/microphone/picovoice/demo/c/build/pvrecorder
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_circular_buffer "/home/debian/microphone/picovoice/demo/c/build/pvrecorder/test_circular_buffer")
set_tests_properties(test_circular_buffer PROPERTIES  _BACKTRACE_TRIPLES "/home/debian/microphone/picovoice/demo/c/pvrecorder/CMakeLists.txt;50;add_test;/home/debian/microphone/picovoice/demo/c/pvrecorder/CMakeLists.txt;0;")
subdirs("node")
