#include <image.h>
#include <OS.h>
#include <File.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define AREA_NAME "shared_area"
#define PAGE_SIZE B_PAGE_SIZE

status_t write_to_area(const char *area_name, const void *buf, size_t len);
status_t read_from_file_into_area(const char *filename, area_id *area_id_out);
void test_area();

int main() {
    test_area();
    return 0;
}

void test_area() {
    area_id my_area;
    char *area_addr;
    status_t err;

    my_area = create_area(AREA_NAME, (void **)&area_addr, B_ANY_ADDRESS,
                          PAGE_SIZE * 10, B_NO_LOCK,
                          B_READ_AREA | B_WRITE_AREA);
    
    if (my_area < 0) {
        printf("FAIL: Failed to create area: %s\n", strerror(my_area));
        return;
    }

    for (int i = 0; i < PAGE_SIZE * 5; i++)
        area_addr[i] = system_time() % 256;

    memcpy(area_addr + PAGE_SIZE * 5, area_addr, PAGE_SIZE * 5);

    strcpy(area_addr, "Hello, Area!");

    printf("First message in area: %s\n", area_addr);

    delete_area(my_area);

	system("echo \"test\" > /tmp/testarea");

    area_id file_area;
    err = read_from_file_into_area("/tmp/testarea", &file_area);
    if (err == B_OK) {
        printf("File read into area successfully.\n");
        delete_area(file_area);
    } else {
        printf("FAIL: Error reading file into area: %s\n", strerror(err));
    }
}

status_t read_from_file_into_area(const char *pathname, area_id *area_id_out) {
    status_t err;
    char *area_addr;
    BFile file(pathname, B_READ_ONLY);
    
    if ((err = file.InitCheck()) != B_OK) {
        printf("%s: Can't find or open.\n", pathname);
        return err;
    }

    off_t file_size;
    err = file.GetSize(&file_size);
    
    if (err != B_OK || file_size == 0) {
        printf("FAIL: %s: Disappeared? Empty?\n", pathname);
        return err;
    }

    off_t area_size = ((file_size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    area_id area = create_area("File area", (void **)&area_addr,
                                B_ANY_ADDRESS, area_size, B_FULL_LOCK,
                                B_READ_AREA | B_WRITE_AREA);

    if (area < 0) {
        printf("FAIL: Failed to create area for file: %s\n", strerror(area));
        return area;
    }

    if ((err = file.Read(area_addr, file_size)) < B_OK) {
        printf("FAIL: %s: File read error.\n", pathname);
        delete_area(area);
        return err;
    }

    printf("File contains: %s\n", area_addr);


    *area_id_out = area;
    return B_OK;
}
