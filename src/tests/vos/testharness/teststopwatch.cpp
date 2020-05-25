#include <StopWatch.h>
#include <stdio.h>



int main(void)
{
	BStopWatch*	aTimer;
	int		x;
	int		y;	

	puts("About to create StopWatch");
	aTimer = new BStopWatch("1", false);
	puts("StopWatch created");
	for (x = 0; x < 10; x++)
	{
		for (y = 0; y < 100000; y++)
		{
			x++;
			x--;
		}

		printf("%lld microseconds elapsed on stopwatch %s\n",
			aTimer->ElapsedTime(),
			aTimer->Name());
	}

	aTimer->Reset();

	for (y = 0; y < 100000; y++)
	{
		x++;
		x--;
	}

	aTimer->Lap();

	{
		BStopWatch	stackWatch("Stack", false);
		
		for (y = 0; y < 100000; y++)
		{
			x++;
			x--;
		}
	}

	aTimer->Lap();

	delete aTimer;

	aTimer = new BStopWatch("2", false);

	{
		BStopWatch stackWatch("Stack2", false);
		
		for (y = 0; y < 100000; y++)
		{
			x++;
			x--;
		}
		
		stackWatch.Lap();
		aTimer->Suspend();
		
		for (y = 0; y < 100000; y++)
		{
			x++;
			x--;
		}
		
		aTimer->Resume();
		stackWatch.Lap();

		for (y = 0; y < 100000; y++)
		{
			x++;
			x--;
		}
	}

	delete aTimer;

	return 0;
}

