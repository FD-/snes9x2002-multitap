In order to be able to play 4-player games on the Raspberry Pi Zero, I tried 
to backport the controls code from the snes9x master (which is slow, but multitap works) 
to snes9x2002 (fast, but multitap doesn't work). 

Unfortunately, I didn't actually succeed:
Although games now correctly detect when an input device is set to multitap,
any controller attached to the multitap doesn't work. I don't really 
have an idea what might be causing the problem (could it be related to lack of
OpenBus in 2002?). 
AFAICT, all calls from snes9x into the controls code (controls.cpp) are 
identical to those of the snes9x master.
