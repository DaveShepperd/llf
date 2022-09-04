# LLF
LLF is a Link+Locate+Format tool

This is pretty old code. The first implementation of a linker we used at Atari coin-op was LINKM which I wrote in PDP11 assembly for the RT11 O/S in 1976. In the early 1980's (probably 1982 or 1983) I wrote this tool in C to replace the three separate tools, Link, Locate, Format cross development tools from GreeHills (or maybe it was Intermetrics, I forget). We started using those cross-development tools on the VAX/VMS when we started developing for the 68000 microprocessors we were using in our game hardware. These separate tools were very slow on VMS so I took it upon myself to make something that worked much faster, at least under VAX/VMS.

Since then LLF has been ported to lots of different O/S and had some bug fixes and features added, however, in the last 20+ years it has only been used (by me that I know of) on Linux systems. Since some old Atari game sources were posted to github recently, I thought I'd post this tool so others might be able to use it to help to build some of them.

Note, this tool is NOT LINKM, however, it will read and parse .obj files produced by the RT11 version of macxx. I don't remember if that particular feature has been well tested. It will link the ouput from the current C version of macxx but you will have to write new scripts/build procedures to replace the expected build scripts using LINKM used by the old game projects.
