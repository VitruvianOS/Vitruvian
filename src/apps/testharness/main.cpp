#include <VolumeRoster.h>
#include <stdio.h>



int main(void)
{
	BVolumeRoster	aRoster;
	BVolume*	aVolume;
	int		count = 0L;
	char*		aVolumeName;
	
	aVolume = new BVolume;
	aVolumeName = new char[256];
	
	while (aRoster.GetNextVolume(aVolume) != -1)
	{
		aVolume->GetName(aVolumeName);
		printf("Found Volume \"%s\"\n", aVolumeName);
		count++;
	}

	printf("\nFound %d volumes in total\n", count);

	delete[] aVolumeName;
	delete aVolume;
	
	return 0;
}
