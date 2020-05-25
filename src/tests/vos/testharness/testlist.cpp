#include <VolumeRoster.h>
#include <string.h>
#include <stdio.h>



bool Prepend(void* inItem);

int main(void)
{
	BList	aList;
	char*	aName;
	int		x;

	for (x = 0; x < 10; x++)
	{
		aName = new char[256];

		aName[0] = '0' + x;
		strcpy(aName + 1, "List Item");
		aList.AddItem(aName);
	}

	printf("Number of list items: %ld\n", aList.CountItems());

	for (x = 0; x < 10; x++)
	{
		aName = (char*)aList.ItemAt(x);
		printf("Item #%d contains \"%s\"\n", x, aName);
	}

	printf("\nThe first item is: \"%s\"\n", (char*)aList.FirstItem());
	printf("The last item is : \"%s\"\n\n", (char*)aList.LastItem());

	aName = (char*)aList.RemoveItem(5);
	delete[] aName;
	aName = new char[256];
	strcpy(aName, "this is the replacement item 5");
	aList.AddItem(aName, 5);
	puts("Item 5 was replaced");

	aList.SwapItems(7, 8);
	puts("Items 7 & 8 were swapped");

	aList.MoveItem(3, 0);
	puts("Item 3 moved to the front");

	aList.DoForEach(Prepend);
	puts("\" - Appended\" appended to each item\n");

	printf("An item at an invalid index returned %ld\n", aList.ItemAt(10));

	for (x = 0; x < 10; x++)
	{
		aName = (char*)aList.RemoveItem((int32)0);
		printf("Item %d which said \"%s\" was removed\n", x, aName);
	}

	printf("Number of list items: %ld\n", aList.CountItems());

	delete[] aName;

	return 0;
}


bool Prepend(void* inItem)
{
	char* testString = " - Appended";
	strcpy((char*)inItem, strcat((char*)inItem, testString));

	return false;
}
