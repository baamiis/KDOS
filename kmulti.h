// KMulti.h

// Main header file for project, should be included by all modules before any
// other header files.
//
// Keeps the project history. Controls debugging, version numbers and other
// conditional compilation. can Hold some hardware definitions for system initialisation.

/* History
   =======
Authors:
  KH  Khalid Hamdou

Ver  Date       By  Description
===  ====       ==  ===========
0.01 26/08/2001 RF  First release for review by KH

// =========
// DEBUG is system wide and should be either ON or OFF (1 or 0).
// Other modules may enable debug only if DEBUG == ON.
// Set DEBUG to OFF to compile out all debug code.
#ifndef DEBUG
  #define DEBUG                    0
#endif
