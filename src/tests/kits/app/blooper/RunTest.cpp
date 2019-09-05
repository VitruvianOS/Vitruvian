//------------------------------------------------------------------------------
//	RunTest.cpp
//
//------------------------------------------------------------------------------

// Standard Includes -----------------------------------------------------------

// System Includes -------------------------------------------------------------
#include <Looper.h>

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------
#include "RunTest.h"

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------

//------------------------------------------------------------------------------
/**
	Run()
	@case		Attempt to call Run() twice
	@results	debugger message "can't call BLooper::Run twice!"
 */
void TRunTest::RunTest1()
{
	DEBUGGER_ESCAPE;

	BLooper Looper;
	Looper.Run();
	Looper.Run();
	Looper.Quit();
}
//------------------------------------------------------------------------------
/**
	Run()
	@case		Check thread_id of Looper
	@results	Run() and Thread() return the same thread_id
 */
void TRunTest::RunTest2()
{
	BLooper* Looper = new BLooper;
	thread_id tid = Looper->Run();
	CPPUNIT_ASSERT(tid == Looper->Thread());
	Looper->Lock();
	Looper->Quit();
}
//------------------------------------------------------------------------------
/**
	Run()
	@case		Delete looper after calling Run()
	@results	Debugger message "You can't call delete on a BLooper object
				once it is running."
 */
void TRunTest::RunTest3()
{
	BLooper* Looper = new BLooper;
	Looper->Run();
	delete Looper;
}
//------------------------------------------------------------------------------
#ifdef ADD_TEST
#undef ADD_TEST
#endif
#define ADD_TEST(__test_name__)	\
	ADD_TEST4(BLooper, suite, TRunTest, __test_name__)

TestSuite* TRunTest::Suite()
{
	TestSuite* suite = new TestSuite("BLooper::Run()");

	ADD_TEST(RunTest1);
	ADD_TEST(RunTest2);

	return suite;
}
//------------------------------------------------------------------------------

/*
 * $Log $
 *
 * $Id  $
 *
 */

