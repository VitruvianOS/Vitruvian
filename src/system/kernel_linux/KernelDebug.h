/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _KERNEL_DEBUG_H
#define _KERNEL_DEBUG_H

#ifndef DEBUG
  #define DEBUG 3
#endif

#include <Debug.h>
#include <stdio.h>

#undef TRACE
#undef PRINT
#undef UNIMPLEMENTED


#if DEBUG > 0

  #define UNIMPLEMENTED()		printf("UNIMPLEMENTED %s\n",__PRETTY_FUNCTION__)

  #if DEBUG >= 2
	#define BROKEN()			printf("BROKEN %s\n",__PRETTY_FUNCTION__)
	#define TRACE 				printf
  #else
  	#define BROKEN()			((void)0)
	#define TRACE(a...)			((void)0)
  #endif

  #if DEBUG >= 3
	#define CALLED() 			printf("CALLED %s\n",__PRETTY_FUNCTION__)
  #else
  	#define CALLED() 			((void)0)
  #endif

#else

	#define PRINT_FORMAT(_text, _fmt)	((void)0)
	#define PRINT_INPUT(_text, _in)		((void)0)
	#define PRINT_OUTPUT(_text, _out)	((void)0)
	#define UNIMPLEMENTED() 			printf("UNIMPLEMENTED %s\n",__PRETTY_FUNCTION__)
	#define BROKEN()					((void)0)
	#define CALLED()					((void)0)
	#define PRINT(l, a...)				((void)0)
	#define ERROR(a...)					fprintf(stderr, a)
	#define TRACE(a...)					((void)0)

#endif

#endif
