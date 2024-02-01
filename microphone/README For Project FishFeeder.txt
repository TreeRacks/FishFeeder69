All code for FishFeeder was integrated into microphone/picovoice/demo/c/picovoice_demo_mic
rest of code/files is from Picocvoice for speech recognition, we have changed the keywords for wake up and intent.
also 80% of the code in picovoice_demo_mic was from Picocvoice, while the other 20% was done by the group

To Build:
($Host) cmake -S demo/c/. -B demo/c/build && cmake --build demo/c/build --target picovoice_demo_mic

To Run the executable:
./demo/c/build/picovoice_demo_mic \
-a c2F98tTS0udD1SkTqCUMiGWfVazKXK84orPgBCrQLUDfoSXj8ABhBg== \
-l sdk/c/lib/beaglebone/libpicovoice.so \
-p resources/porcupine/lib/common/porcupine_params.pv \
-k resources/porcupine/resources/keyword_files/beaglebone/Hi-siri_en_beaglebone_v2_1_0.ppn \
-r resources/rhino/lib/common/rhino_params.pv \
-c resources/rhino/resources/contexts/beaglebone/Release-the-food_en_beaglebone_v2_1_0.rhn \
-i 1
