#include <VolumeRoster.h>
#include <stdio.h>



int main(void)
{
	BVolumeRoster	aRoster;
	BVolume*	aVolume;
	int		count = 0L;
	char*		aVolumeName;
	
	aVolume = new BVolume;
	aVolumeName = new char[B_FILE_NAME_LENGTH];
	
	while (aRoster.GetNextVolume(aVolume) == B_OK) {
		aVolume->GetName(aVolumeName);
		printf("Found Volume \"%s\"\n", aVolumeName);
		count++;
	}

	printf("\nFound %d volumes in total\n", count);

	delete[] aVolumeName;
	delete aVolume;
	
	return 0;
}
