#include <OS.h>

#include <stdlib.h>
#include <sys/wait.h>


void
readfd(int fd) {
		char buffer[1024];
		ssize_t bytesRead;
		bytesRead = read(fd, buffer, sizeof(buffer) - 1);

		if (bytesRead > 0) {
			buffer[bytesRead] = '\0';

			for(int i = 0; i < bytesRead; i++) {
				if (buffer[i] == '\n') {
					buffer[i] = '\0';
					break;
				}
			}
			printf("Fd contains: %s\n", buffer);
			if (strcmp(buffer, "test") != 0)
				printf("FAIL\n");
		} else
			printf("FAIL: couldn't read fd: %s\n", strerror(errno));
}


int
main()
{
	system("echo \"test\" > /tmp/vreftest.txt");
	// clo_exec
	int fd = open("/tmp/vreftest.txt", O_RDONLY | O_CLOEXEC);

	vref_id id = create_vref(fd);
	printf("created vref %d\n", id);

	if (id < 0)
		printf("FAIL: create_vref\n");

	readfd(fd);

    pid_t pid = fork();

    if (pid < 0) {
        perror("FAIL: fork failed");
        return -1;
    }

    if (pid == 0) {
        printf("Hello from the child process! PID: %d\n", getpid());
        // The fd inherits the same permissions as the one in the father
        printf("child acquire %d\n", id);
 		int clone_fd = acquire_vref(id);
 		lseek(clone_fd, 0, SEEK_SET);
 		// stat
 		if (clone_fd < 0) {
			printf("FAIL: acquire_vref child: %s\n", strerror(clone_fd));
			return -1;
		}
		readfd(clone_fd);

		status_t ret = release_vref(id);
		if (ret != B_OK)
			printf("FAIL: release_vref child: %s\n", strerror(ret));

		exit(0);
    } else {
        wait(NULL);
        printf("Child process has terminated.\n");
    }
	snooze(1000);
	close(fd);
	status_t ret = release_vref(id);
	if (ret != B_OK)
		printf("FAIL: release_vref father: %s\n", strerror(ret));

	int clone_fd = acquire_vref(id);
	if (clone_fd >= 0)
		printf("FAIL: acquire_vref for clone and father\n");

    return 0;
}
