//------------------------------------------------------------------------------
//	RunTest.h
//
//------------------------------------------------------------------------------

#ifndef RUNTEST_H
#define RUNTEST_H

// Standard Includes -----------------------------------------------------------

// System Includes -------------------------------------------------------------

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------
#include "../common.h"

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------

class TRunTest : public TestCase
{
	public:
		TRunTest() {;}
		TRunTest(std::string name) : TestCase(name) {;}

		void RunTest1();
		void RunTest2();
		void RunTest3();

		static TestSuite* Suite();
};

#endif	//RUNTEST_H

/*
 * $Log $
 *
 * $Id  $
 *
 */

