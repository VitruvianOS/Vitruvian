/*
 * Copyright 2024 Dario Casalinuovo. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

/*
 * test_node_monitor.cpp - Test suite for haiku_node_monitor using Haiku APIs
 * 
 * This test uses real Haiku KMessage and port APIs to verify that:
 *   1. Node monitor notifications arrive on the correct port
 *   2. KMessage format is correct and parseable by Haiku's KMessage class
 *   3. All notification types work correctly
 * 
 * Build: g++ -O2 test_node_monitor.cpp -o test_node_monitor -lroot
 * Run:   ./test_node_monitor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

/* Haiku headers */
#include <OS.h>
#include <NodeMonitor.h>
#include <util/KMessage.h>

/* Our ioctl interface */
#include "node_monitor.h"


/* ============================================================================
 * Test infrastructure
 * ============================================================================
 */

#define TEST_DIR    "/tmp/haiku_node_monitor_test"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_RESET "\033[0m"

static int tests_run = 0;
static int tests_passed = 0;
static int monitor_fd = -1;
static port_id test_port = -1;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf(COLOR_RED "  FAIL: %s" COLOR_RESET "\n", msg); \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_NE(a, b, msg) ASSERT((a) != (b), msg)
#define ASSERT_GE(a, b, msg) ASSERT((a) >= (b), msg)
#define ASSERT_STREQ(a, b, msg) ASSERT(strcmp(a, b) == 0, msg)
#define ASSERT_OK(status, msg) ASSERT((status) == B_OK, msg)

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("Running: %s...\n", #test_func); \
    if (test_func()) { \
        tests_passed++; \
        printf(COLOR_GREEN "  PASS" COLOR_RESET "\n"); \
    } \
} while(0)


/* ============================================================================
 * Helper functions
 * ============================================================================
 */

static void setup_test_dir()
{
    system("rm -rf " TEST_DIR);
    mkdir(TEST_DIR, 0755);
}

static void cleanup_test_dir()
{
    system("rm -rf " TEST_DIR);
}

/*
 * Read a KMessage from the test port with timeout
 * Returns B_OK on success, B_TIMED_OUT on timeout
 */
static status_t read_message(KMessage& msg, bigtime_t timeout_us)
{
    char buffer[4096];
    ssize_t size;
    int32 code;
    
    size = read_port_etc(test_port, &code, buffer, sizeof(buffer),
                         B_TIMEOUT, timeout_us);
    if (size < 0)
        return size;
    
    if (code != B_NODE_MONITOR)
        return B_BAD_VALUE;
    
    return msg.SetTo(buffer, size);
}

/*
 * Drain any pending messages from the port
 */
static void drain_port()
{
    char buffer[4096];
    int32 code;
    
    while (port_buffer_size_etc(test_port, B_TIMEOUT, 0) > 0) {
        read_port(test_port, &code, buffer, sizeof(buffer));
    }
}


/* ============================================================================
 * Tests
 * ============================================================================
 */

/*
 * Test 1: Basic device and port setup
 */
static int test_basic_setup()
{
    ASSERT_GE(monitor_fd, 0, "Device should be open");
    ASSERT_GE(test_port, 0, "Port should be created");
    return 1;
}

/*
 * Test 2: Watch and unwatch a directory
 */
static int test_watch_unwatch()
{
    int dir_fd = open(TEST_DIR, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(dir_fd, 0, "Should open test directory");
    
    struct stat st;
    fstat(dir_fd, &st);
    
    /* Start watching */
    struct haiku_watch_request wreq = {
        .fd = dir_fd,
        .flags = B_WATCH_DIRECTORY,
        .port = test_port,
        .token = 1
    };
    
    int ret = ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    ASSERT_EQ(ret, 0, "Start watching should succeed");
    
    /* Stop watching */
    struct haiku_unwatch_request ureq = {
        .device = (uint64_t)st.st_dev,
        .node = (uint64_t)st.st_ino,
        .port = test_port,
        .token = 1
    };
    
    ret = ioctl(monitor_fd, HAIKU_IOC_STOP_WATCHING, &ureq);
    ASSERT_EQ(ret, 0, "Stop watching should succeed");
    
    close(dir_fd);
    return 1;
}

/*
 * Test 3: File creation notification with KMessage parsing
 */
static int test_entry_created()
{
    KMessage msg;
    
    int dir_fd = open(TEST_DIR, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(dir_fd, 0, "Should open test directory");
    
    struct stat st;
    fstat(dir_fd, &st);
    
    /* Start watching */
    struct haiku_watch_request wreq = {
        .fd = dir_fd,
        .flags = B_WATCH_DIRECTORY,
        .port = test_port,
        .token = 100
    };
    
    int ret = ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    ASSERT_EQ(ret, 0, "Start watching should succeed");
    
    drain_port();
    
    /* Create a file */
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/testfile.txt", TEST_DIR);
    int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
    ASSERT_GE(fd, 0, "Should create test file");
    close(fd);
    
    /* Read notification */
    status_t status = read_message(msg, 1000000); /* 1 second timeout */
    ASSERT_OK(status, "Should receive notification");
    
    /* Verify KMessage contents using Haiku's KMessage API */
    ASSERT_EQ(msg.What(), B_NODE_MONITOR, "Message what should be B_NODE_MONITOR");
    
    int32 opcode;
    ASSERT_OK(msg.FindInt32("opcode", &opcode), "Should have opcode field");
    ASSERT_EQ(opcode, B_ENTRY_CREATED, "Opcode should be B_ENTRY_CREATED");
    
    entry_ref dirRef;
    ASSERT_OK(msg.FindRef("virtual:directory", &dirRef), "Should have virtual:directory field");

    node_ref nodeRef;
    ASSERT_OK(msg.FindNodeRef("virtual:node", &nodeRef), "Should have virtual:node field");
    
    const char *name;
    ASSERT_OK(msg.FindString("name", &name), "Should have name field");
    ASSERT_STREQ(name, "testfile.txt", "Name should be 'testfile.txt'");
    
    /* Cleanup */
    struct haiku_unwatch_request ureq = {
        .device = (uint64_t)st.st_dev,
        .node = (uint64_t)st.st_ino,
        .port = test_port,
        .token = 100
    };
    ioctl(monitor_fd, HAIKU_IOC_STOP_WATCHING, &ureq);
    
    close(dir_fd);
    unlink(filepath);
    
    return 1;
}

/*
 * Test 4: File deletion notification
 */
static int test_entry_removed()
{
    KMessage msg;
    
    /* Create file first */
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/todelete.txt", TEST_DIR);
    int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
    close(fd);
    
    int dir_fd = open(TEST_DIR, O_RDONLY | O_DIRECTORY);
    struct stat st;
    fstat(dir_fd, &st);
    
    /* Start watching */
    struct haiku_watch_request wreq = {
        .fd = dir_fd,
        .flags = B_WATCH_DIRECTORY,
        .port = test_port,
        .token = 200
    };
    ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    drain_port();
    
    /* Delete the file */
    unlink(filepath);
    
    /* Read notification */
    status_t status = read_message(msg, 1000000);
    ASSERT_OK(status, "Should receive notification");
    
    int32 opcode;
    ASSERT_OK(msg.FindInt32("opcode", &opcode), "Should have opcode field");
    ASSERT_EQ(opcode, B_ENTRY_REMOVED, "Opcode should be B_ENTRY_REMOVED");
    
    const char *name;
    ASSERT_OK(msg.FindString("name", &name), "Should have name field");
    ASSERT_STREQ(name, "todelete.txt", "Name should be 'todelete.txt'");
    
    /* Cleanup */
    struct haiku_unwatch_request ureq = {
        .device = (uint64_t)st.st_dev,
        .node = (uint64_t)st.st_ino,
        .port = test_port,
        .token = 200
    };
    ioctl(monitor_fd, HAIKU_IOC_STOP_WATCHING, &ureq);
    close(dir_fd);
    
    return 1;
}

/*
 * Test 5: File rename (B_ENTRY_MOVED)
 */
static int test_entry_moved()
{
    KMessage msg;
    
    /* Create file */
    char oldpath[256], newpath[256];
    snprintf(oldpath, sizeof(oldpath), "%s/oldname.txt", TEST_DIR);
    snprintf(newpath, sizeof(newpath), "%s/newname.txt", TEST_DIR);
    int fd = open(oldpath, O_CREAT | O_WRONLY, 0644);
    close(fd);
    
    int dir_fd = open(TEST_DIR, O_RDONLY | O_DIRECTORY);
    struct stat st;
    fstat(dir_fd, &st);
    
    /* Start watching */
    struct haiku_watch_request wreq = {
        .fd = dir_fd,
        .flags = B_WATCH_DIRECTORY,
        .port = test_port,
        .token = 300
    };
    ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    drain_port();
    
    /* Rename file */
    rename(oldpath, newpath);
    
    /* Read notification */
    status_t status = read_message(msg, 1000000);
    ASSERT_OK(status, "Should receive notification");
    
    int32 opcode;
    ASSERT_OK(msg.FindInt32("opcode", &opcode), "Should have opcode field");
    ASSERT_EQ(opcode, B_ENTRY_MOVED, "Opcode should be B_ENTRY_MOVED");
    
    int64 from_dir, to_dir;
    ASSERT_OK(msg.FindInt64("from directory", &from_dir), "Should have from directory");
    ASSERT_OK(msg.FindInt64("to directory", &to_dir), "Should have to directory");
    ASSERT_EQ(from_dir, to_dir, "Same directory rename");
    
    const char *name;
    ASSERT_OK(msg.FindString("name", &name), "Should have name field");
    ASSERT_STREQ(name, "newname.txt", "Name should be new name");
    
    const char *from_name;
    ASSERT_OK(msg.FindString("from name", &from_name), "Should have from name field");
    ASSERT_STREQ(from_name, "oldname.txt", "From name should be old name");
    
    /* Cleanup */
    struct haiku_unwatch_request ureq = {
        .device = (uint64_t)st.st_dev,
        .node = (uint64_t)st.st_ino,
        .port = test_port,
        .token = 300
    };
    ioctl(monitor_fd, HAIKU_IOC_STOP_WATCHING, &ureq);
    close(dir_fd);
    unlink(newpath);
    
    return 1;
}

/*
 * Test 6: Stat change notification
 */
static int test_stat_changed()
{
    KMessage msg;
    
    /* Create file */
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/stattest.txt", TEST_DIR);
    int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
    write(fd, "hello", 5);
    close(fd);
    
    /* Watch the file itself */
    fd = open(filepath, O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    
    struct haiku_watch_request wreq = {
        .fd = fd,
        .flags = B_WATCH_STAT,
        .port = test_port,
        .token = 400
    };
    ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    drain_port();
    close(fd);
    
    /* Modify the file */
    fd = open(filepath, O_WRONLY | O_APPEND);
    write(fd, " world", 6);
    close(fd);
    
    /* Read notification */
    status_t status = read_message(msg, 1000000);
    ASSERT_OK(status, "Should receive notification");
    
    int32 opcode;
    ASSERT_OK(msg.FindInt32("opcode", &opcode), "Should have opcode field");
    ASSERT_EQ(opcode, B_STAT_CHANGED, "Opcode should be B_STAT_CHANGED");
    
    node_ref nodeRef;
    ASSERT_OK(msg.FindNodeRef("virtual:node", &nodeRef), "Should have virtual:node field");
    
    int32 fields;
    ASSERT_OK(msg.FindInt32("fields", &fields), "Should have fields");
    ASSERT(fields & B_STAT_SIZE, "Should include B_STAT_SIZE");
    
    /* Cleanup */
    struct haiku_unwatch_request ureq = {
        .device = (uint64_t)st.st_dev,
        .node = (uint64_t)st.st_ino,
        .port = test_port,
        .token = 400
    };
    ioctl(monitor_fd, HAIKU_IOC_STOP_WATCHING, &ureq);
    unlink(filepath);
    
    return 1;
}

/*
 * Test 7: B_WATCH_CHILDREN recursive watching
 */
static int test_watch_children()
{
    KMessage msg;
    
    /* Create parent directory */
    char parent[256], subdir[256], filepath[256];
    snprintf(parent, sizeof(parent), "%s/parent", TEST_DIR);
    mkdir(parent, 0755);
    
    int dir_fd = open(parent, O_RDONLY | O_DIRECTORY);
    struct stat st;
    fstat(dir_fd, &st);
    
    /* Watch with B_WATCH_CHILDREN */
    struct haiku_watch_request wreq = {
        .fd = dir_fd,
        .flags = B_WATCH_DIRECTORY | B_WATCH_CHILDREN,
        .port = test_port,
        .token = 500
    };
    int ret = ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    ASSERT_EQ(ret, 0, "B_WATCH_CHILDREN should succeed");
    drain_port();
    
    /* Create subdirectory */
    snprintf(subdir, sizeof(subdir), "%s/parent/child", TEST_DIR);
    mkdir(subdir, 0755);
    
    /* Should get notification for subdir creation */
    status_t status = read_message(msg, 1000000);
    ASSERT_OK(status, "Should receive subdir notification");
    
    int32 opcode;
    msg.FindInt32("opcode", &opcode);
    ASSERT_EQ(opcode, B_ENTRY_CREATED, "Should be B_ENTRY_CREATED");
    
    const char *name;
    msg.FindString("name", &name);
    ASSERT_STREQ(name, "child", "Name should be 'child'");
    
    /* Now create file in subdirectory - should also get notification */
    snprintf(filepath, sizeof(filepath), "%s/parent/child/file.txt", TEST_DIR);
    int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
    close(fd);
    
    status = read_message(msg, 1000000);
    ASSERT_OK(status, "Should receive file notification from child");
    
    msg.FindInt32("opcode", &opcode);
    ASSERT_EQ(opcode, B_ENTRY_CREATED, "Should be B_ENTRY_CREATED for file");
    
    msg.FindString("name", &name);
    ASSERT_STREQ(name, "file.txt", "Name should be 'file.txt'");
    
    /* Cleanup */
    struct haiku_unwatch_request ureq = {
        .device = (uint64_t)st.st_dev,
        .node = (uint64_t)st.st_ino,
        .port = test_port,
        .token = 500
    };
    ioctl(monitor_fd, HAIKU_IOC_STOP_WATCHING, &ureq);
    close(dir_fd);
    
    unlink(filepath);
    rmdir(subdir);
    rmdir(parent);
    
    return 1;
}

/*
 * Test 8: Stop notifying removes all watches for port/token
 */
static int test_stop_notifying()
{
    int dir_fd = open(TEST_DIR, O_RDONLY | O_DIRECTORY);
    struct stat st;
    fstat(dir_fd, &st);
    
    /* Start multiple watches */
    struct haiku_watch_request wreq = {
        .fd = dir_fd,
        .flags = B_WATCH_DIRECTORY,
        .port = test_port,
        .token = 600
    };
    ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    
    wreq.token = 601;
    ioctl(monitor_fd, HAIKU_IOC_START_WATCHING, &wreq);
    
    drain_port();
    
    /* Stop all watches for token 600 */
    struct haiku_stop_notifying_request sreq = {
        .port = test_port,
        .token = 600
    };
    int ret = ioctl(monitor_fd, HAIKU_IOC_STOP_NOTIFYING, &sreq);
    ASSERT_EQ(ret, 0, "Stop notifying should succeed");
    
    /* Create a file */
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/stoptest.txt", TEST_DIR);
    int fd = open(filepath, O_CREAT | O_WRONLY, 0644);
    close(fd);
    
    /* Should still get notification from token 601 */
    KMessage msg;
    status_t status = read_message(msg, 500000);
    ASSERT_OK(status, "Should still receive notification from token 601");
    
    /* Clean up remaining watch */
    struct haiku_unwatch_request ureq = {
        .device = (uint64_t)st.st_dev,
        .node = (uint64_t)st.st_ino,
        .port = test_port,
        .token = 601
    };
    ioctl(monitor_fd, HAIKU_IOC_STOP_WATCHING, &ureq);
    
    close(dir_fd);
    unlink(filepath);
    
    return 1;
}


/* ============================================================================
 * Main
 * ============================================================================
 */

int main(int argc, char **argv)
{
    printf("=== Haiku Node Monitor Test Suite (KMessage/Ports) ===\n\n");
    
    /* Open device */
    monitor_fd = open("/dev/" HAIKU_NODE_MONITOR_DEVICE, O_RDWR);
    if (monitor_fd < 0) {
        perror("Failed to open /dev/haiku_node_monitor");
        printf("\nIs the kernel module loaded? Try: sudo insmod haiku_node_monitor.ko\n");
        return 1;
    }
    
    /* Create test port */
    test_port = create_port(100, "node_monitor_test");
    if (test_port < 0) {
        fprintf(stderr, "Failed to create port: %s\n", strerror(test_port));
        close(monitor_fd);
        return 1;
    }
    
    setup_test_dir();
    
    /* Run tests */
    RUN_TEST(test_basic_setup);
    RUN_TEST(test_watch_unwatch);
    RUN_TEST(test_entry_created);
    RUN_TEST(test_entry_removed);
    RUN_TEST(test_entry_moved);
    RUN_TEST(test_stat_changed);
    RUN_TEST(test_watch_children);
    RUN_TEST(test_stop_notifying);
    
    cleanup_test_dir();
    delete_port(test_port);
    close(monitor_fd);
    
    /* Summary */
    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf(COLOR_GREEN "\nAll tests passed!" COLOR_RESET "\n");
        return 0;
    } else {
        printf(COLOR_RED "\nSome tests failed!" COLOR_RESET "\n");
        return 1;
    }
}
