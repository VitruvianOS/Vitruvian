#include <OS.h>

int main()
{
	sem_id sem = create_sem(0,  "sem");
	delete_sem(sem);
}
