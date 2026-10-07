/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/MidiText.h>

#include <string.h>


BMidiText::BMidiText()
	:
	BMidi()
{
	fStartTime = 0;
	memset(_reserved, 0, sizeof(_reserved));
}


BMidiText::~BMidiText()
{
	Stop();
}


void
BMidiText::ResetTimer(bool start)
{
	fStartTime = start ? B_NOW : 0;
}


void
BMidiText::_WaitAndPrint(uint32 time)
{
	if (fStartTime != 0)
		SnoozeUntil(fStartTime + time);
	printf("%6u  ", time);
}


void
BMidiText::NoteOff(uchar channel, uchar note, uchar velocity, uint32 time)
{
	_WaitAndPrint(time);
	printf("note off   ch %2d  note %3d  vel %3d\n", channel, note, velocity);
}


void
BMidiText::NoteOn(uchar channel, uchar note, uchar velocity, uint32 time)
{
	_WaitAndPrint(time);
	printf("note on    ch %2d  note %3d  vel %3d\n", channel, note, velocity);
}


void
BMidiText::KeyPressure(uchar channel, uchar note, uchar pressure, uint32 time)
{
	_WaitAndPrint(time);
	printf("key press  ch %2d  note %3d  val %3d\n", channel, note, pressure);
}


void
BMidiText::ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, uint32 time)
{
	_WaitAndPrint(time);
	printf("control    ch %2d  num  %3d  val %3d\n", channel, controlNumber,
		controlValue);
}


void
BMidiText::ProgramChange(uchar channel, uchar programNumber, uint32 time)
{
	_WaitAndPrint(time);
	printf("program    ch %2d  num  %3d\n", channel, programNumber);
}


void
BMidiText::ChannelPressure(uchar channel, uchar pressure, uint32 time)
{
	_WaitAndPrint(time);
	printf("chan press ch %2d  val %3d\n", channel, pressure);
}


void
BMidiText::PitchBend(uchar channel, uchar lsb, uchar msb, uint32 time)
{
	_WaitAndPrint(time);
	printf("pitch bend ch %2d  lsb  %3d  msb %3d\n", channel, lsb, msb);
}


void
BMidiText::SystemExclusive(void* data, size_t length, uint32 time)
{
	_WaitAndPrint(time);
	printf("sys ex     %u bytes\n", (unsigned)length);
	(void)data;
}


void
BMidiText::SystemCommon(uchar status, uchar data1, uchar data2, uint32 time)
{
	_WaitAndPrint(time);
	printf("sys common %02x %02x %02x\n", status, data1, data2);
}


void
BMidiText::SystemRealTime(uchar status, uint32 time)
{
	_WaitAndPrint(time);
	printf("sys real   %02x\n", status);
}


void
BMidiText::Run()
{
	while (KeepRunning())
		snooze(20000);
}


void
BMidiText::_ReservedMidiText1()
{
}


void
BMidiText::_ReservedMidiText2()
{
}


void
BMidiText::_ReservedMidiText3()
{
}
