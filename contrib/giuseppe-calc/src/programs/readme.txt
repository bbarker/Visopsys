This is my implementation of a calculator for the Visopsys operating system.
Visopsys is free and open source and you can get it at http://visopsys.org

This program was tested on Visopsys 0.72. This archive also includes a precompiled binary that is ready to use.

The easiest way to compile this program is to put calc.c in the Visopsys source tree in src/programs and to add calc to the programs that should be compiled.

The usage of this calculator does not need to be explained, as it is obvious, some things need to be clarified, though: the button labelled "dec" actually changes the current numeric base, if you press it once the current numeric base will be hexadecimal and the button will now be labelled "hex" and if you press it twice the base will be octal and the button labelled "oct". Pressing it three times restarts the cycle with "dec" and so on...
Floating point behavior might look a bit strange to people not accustomed to binary floating point operation: in fact, after typing some floating number, you might see it gets turned into another number. This happens due to the structure of binary floating pointer numbers; you can't represent many decimal floating point numbers precisely with binary floating pointer numbers. The only workaround to this would be using a software decimal floating point library.

This program is licensed under a two-clauses BSD license.

-- Giuseppe Gatta <tails92@gmail.com>
September 1st, 2013