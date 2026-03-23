/*
 * test_node_monitor2.cpp - Test using actual Haiku NodeMonitor API
 * 
 * This test uses the real Haiku Storage Kit API (watch_node, stop_watching,
 * BLooper, BHandler) to test the Linux node monitor implementation.
 * 
 * Compile: g++ -std=c++11 -o test_node_monitor2 test_node_monitor2.cpp -lbe -lroot
 * 
 * Copyright 2024 Dario Casalinuovo. All rights reserved.
 * SPDX-License-Identifier: MIT
 */

#include <Application.h>
#include <Entry.h>
#include <Directory.h>
#include <File.h>
#include <Looper.h>
#include <Handler.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>


/* ============================================================================
 * Test Handler - receives node monitor messages
 * ============================================================================
 */

class TestHandler : public BHandler {
public:
    TestHandler(const char* name)
        : BHandler(name),
          fEventCount(0),
          fLastOpcode(0)
    {
    }
    
    virtual void MessageReceived(BMessage* message)
    {
        if (message->what == B_NODE_MONITOR) {
            fEventCount++;
            
            int32 opcode;
            if (message->FindInt32("opcode", &opcode) == B_OK) {
                fLastOpcode = opcode;
                
                printf("[%s] Received: ", Name());
                PrintOpcode(opcode);
                PrintMessageDetails(message, opcode);
            }
        } else {
            BHandler::MessageReceived(message);
        }
    }
    
    int32 EventCount() const { return fEventCount; }
    int32 LastOpcode() const { return fLastOpcode; }
    void ResetCount() { fEventCount = 0; }
    
private:
    void PrintOpcode(int32 opcode)
    {
        switch (opcode) {
            case B_ENTRY_CREATED:
                printf("B_ENTRY_CREATED");
                break;
            case B_ENTRY_REMOVED:
                printf("B_ENTRY_REMOVED");
                break;
            case B_ENTRY_MOVED:
                printf("B_ENTRY_MOVED");
                break;
            case B_STAT_CHANGED:
                printf("B_STAT_CHANGED");
                break;
            case B_ATTR_CHANGED:
                printf("B_ATTR_CHANGED");
                break;
            case B_DEVICE_MOUNTED:
                printf("B_DEVICE_MOUNTED");
                break;
            case B_DEVICE_UNMOUNTED:
                printf("B_DEVICE_UNMOUNTED");
                break;
            default:
                printf("UNKNOWN(%d)", opcode);
        }
    }
    
    void PrintMessageDetails(BMessage* msg, int32 opcode)
    {
        const char* name;
        node_ref nodeRef;
        entry_ref dirRef;

        switch (opcode) {
            case B_ENTRY_CREATED:
            case B_ENTRY_REMOVED:
                if (msg->FindString("name", &name) == B_OK)
                    printf(" name=\"%s\"", name);
                if (msg->FindRef("virtual:directory", &dirRef) == B_OK)
                    printf(" dir_dev=%llu dir=%lld", (unsigned long long)dirRef.device,
                        (long long)dirRef.directory);
                if (msg->FindNodeRef("virtual:node", &nodeRef) == B_OK)
                    printf(" node_dev=%llu node=%lld", (unsigned long long)nodeRef.device,
                        (long long)nodeRef.node);
                break;

            case B_ENTRY_MOVED:
                if (msg->FindString("name", &name) == B_OK)
                    printf(" name=\"%s\"", name);
                if (msg->FindRef("virtual:from directory", &dirRef) == B_OK)
                    printf(" from_dir=%lld", (long long)dirRef.directory);
                if (msg->FindRef("virtual:to directory", &dirRef) == B_OK)
                    printf(" to_dir=%lld", (long long)dirRef.directory);
                break;

            case B_STAT_CHANGED:
                {
                    int32 fields;
                    if (msg->FindInt32("fields", &fields) == B_OK)
                        printf(" fields=0x%x", fields);
                }
                break;

            case B_ATTR_CHANGED:
                if (msg->FindString("attr", &name) == B_OK)
                    printf(" attr=\"%s\"", name);
                {
                    int32 cause;
                    if (msg->FindInt32("cause", &cause) == B_OK) {
                        printf(" cause=");
                        switch (cause) {
                            case B_ATTR_CREATED: printf("CREATED"); break;
                            case B_ATTR_REMOVED: printf("REMOVED"); break;
                            case B_ATTR_CHANGED: printf("CHANGED"); break;
                            default: printf("%d", cause);
                        }
                    }
                }
                break;

            case B_DEVICE_MOUNTED:
                {
                    partition_id newDev;
                    if (msg->FindUInt64("new device", (uint64*)&newDev) == B_OK)
                        printf(" new_device=%llu", (unsigned long long)newDev);
                }
                break;

            case B_DEVICE_UNMOUNTED:
                {
                    partition_id dev;
                    if (msg->FindUInt64("device", (uint64*)&dev) == B_OK)
                        printf(" device=%llu", (unsigned long long)dev);
                }
                break;
        }
        printf("\n");
    }
    
    int32 fEventCount;
    int32 fLastOpcode;
};


/* ============================================================================
 * Test Looper
 * ============================================================================
 */

class TestLooper : public BLooper {
public:
    TestLooper()
        : BLooper("TestLooper")
    {
    }
};


/* ============================================================================
 * Test Functions
 * ============================================================================
 */

static void
WaitForEvents(int32 expectedCount, TestHandler* handler, int timeoutMs = 2000)
{
    int waited = 0;
    while (handler->EventCount() < expectedCount && waited < timeoutMs) {
        snooze(50000);  /* 50ms */
        waited += 50;
    }
}


static bool
TestDirectoryWatch(const char* testDir)
{
    printf("\n=== Test: B_WATCH_DIRECTORY ===\n");
    
    /* Create test directory */
    BDirectory dir;
    if (create_directory(testDir, 0755) != B_OK) {
        printf("Failed to create test directory\n");
        return false;
    }
    dir.SetTo(testDir);
    
    /* Set up watcher */
    TestLooper* looper = new TestLooper();
    TestHandler* handler = new TestHandler("DirWatch");
    looper->AddHandler(handler);
    looper->Run();
    
    node_ref nref;
    dir.GetNodeRef(&nref);
    
    status_t err = watch_node(&nref, B_WATCH_DIRECTORY, handler);
    if (err != B_OK) {
        printf("watch_node failed: %s\n", strerror(err));
        looper->Lock();
        looper->Quit();
        return false;
    }
    printf("Watching directory: %s\n", testDir);
    
    /* Test: Create a file */
    printf("Creating file...\n");
    BString filePath(testDir);
    filePath << "/test_file.txt";
    BFile file(filePath.String(), B_CREATE_FILE | B_WRITE_ONLY);
    file.Write("Hello", 5);
    file.Unset();
    
    WaitForEvents(1, handler);
    
    bool createOk = (handler->LastOpcode() == B_ENTRY_CREATED);
    printf("  B_ENTRY_CREATED: %s\n", createOk ? "PASS" : "FAIL");
    
    /* Test: Delete the file */
    handler->ResetCount();
    printf("Deleting file...\n");
    BEntry entry(filePath.String());
    entry.Remove();
    
    WaitForEvents(1, handler);
    
    bool removeOk = (handler->LastOpcode() == B_ENTRY_REMOVED);
    printf("  B_ENTRY_REMOVED: %s\n", removeOk ? "PASS" : "FAIL");
    
    /* Cleanup */
    stop_watching(handler);
    looper->Lock();
    looper->Quit();
    rmdir(testDir);
    
    return createOk && removeOk;
}


static bool
TestStatWatch(const char* testDir)
{
    printf("\n=== Test: B_WATCH_STAT ===\n");
    
    /* Create test file */
    create_directory(testDir, 0755);
    BString filePath(testDir);
    filePath << "/stat_test.txt";
    
    BFile file(filePath.String(), B_CREATE_FILE | B_WRITE_ONLY);
    file.Write("Initial content", 15);
    file.Unset();
    
    /* Set up watcher */
    TestLooper* looper = new TestLooper();
    TestHandler* handler = new TestHandler("StatWatch");
    looper->AddHandler(handler);
    looper->Run();
    
    BNode node(filePath.String());
    node_ref nref;
    node.GetNodeRef(&nref);
    
    status_t err = watch_node(&nref, B_WATCH_STAT, handler);
    if (err != B_OK) {
        printf("watch_node failed: %s\n", strerror(err));
        looper->Lock();
        looper->Quit();
        return false;
    }
    printf("Watching file: %s\n", filePath.String());
    
    /* Test: Modify file */
    printf("Modifying file...\n");
    file.SetTo(filePath.String(), B_WRITE_ONLY);
    file.Write("Modified!", 9);
    file.Unset();
    
    WaitForEvents(1, handler);
    
    bool modifyOk = (handler->LastOpcode() == B_STAT_CHANGED);
    printf("  B_STAT_CHANGED: %s\n", modifyOk ? "PASS" : "FAIL");
    
    /* Cleanup */
    stop_watching(handler);
    looper->Lock();
    looper->Quit();
    unlink(filePath.String());
    rmdir(testDir);
    
    return modifyOk;
}


static bool
TestNameWatch(const char* testDir)
{
    printf("\n=== Test: B_WATCH_NAME (move/rename) ===\n");
    
    /* Create test file */
    create_directory(testDir, 0755);
    BString filePath(testDir);
    filePath << "/name_test.txt";
    BString newPath(testDir);
    newPath << "/renamed.txt";
    
    BFile file(filePath.String(), B_CREATE_FILE | B_WRITE_ONLY);
    file.Write("Test", 4);
    file.Unset();
    
    /* Set up watcher on the file */
    TestLooper* looper = new TestLooper();
    TestHandler* handler = new TestHandler("NameWatch");
    looper->AddHandler(handler);
    looper->Run();
    
    BNode node(filePath.String());
    node_ref nref;
    node.GetNodeRef(&nref);
    
    status_t err = watch_node(&nref, B_WATCH_NAME, handler);
    if (err != B_OK) {
        printf("watch_node failed: %s\n", strerror(err));
        looper->Lock();
        looper->Quit();
        return false;
    }
    printf("Watching file: %s\n", filePath.String());
    
    /* Test: Rename file */
    printf("Renaming file...\n");
    BEntry entry(filePath.String());
    entry.Rename(newPath.String());
    
    WaitForEvents(1, handler);
    
    bool moveOk = (handler->LastOpcode() == B_ENTRY_MOVED);
    printf("  B_ENTRY_MOVED: %s\n", moveOk ? "PASS" : "FAIL");
    
    /* Cleanup */
    stop_watching(handler);
    looper->Lock();
    looper->Quit();
    unlink(newPath.String());
    rmdir(testDir);
    
    return moveOk;
}


static bool
TestMountWatch()
{
    printf("\n=== Test: B_WATCH_MOUNT ===\n");
    
    /* Set up mount watcher */
    TestLooper* looper = new TestLooper();
    TestHandler* handler = new TestHandler("MountWatch");
    looper->AddHandler(handler);
    looper->Run();
    
    status_t err = watch_node(NULL, B_WATCH_MOUNT, handler);
    if (err != B_OK) {
        printf("watch_node(B_WATCH_MOUNT) failed: %s\n", strerror(err));
        looper->Lock();
        looper->Quit();
        return false;
    }
    printf("Watching for mount events...\n");
    printf("(Insert/remove USB drive or mount/umount filesystem to test)\n");
    printf("Waiting 5 seconds for events...\n");
    
    snooze(5000000);  /* 5 seconds */
    
    int32 count = handler->EventCount();
    printf("Received %d mount events\n", count);
    
    /* Cleanup */
    stop_watching(handler);
    looper->Lock();
    looper->Quit();
    
    /* This test passes if setup succeeded - actual events depend on user action */
    return true;
}


static bool
TestVolumeRoster()
{
    printf("\n=== Test: BVolumeRoster ===\n");
    
    BVolumeRoster roster;
    BVolume volume;
    
    printf("Mounted volumes:\n");
    while (roster.GetNextVolume(&volume) == B_OK) {
        char name[B_FILE_NAME_LENGTH];
        volume.GetName(name);
        printf("  - %s (dev=%d)\n", name, (int)volume.Device());
    }
    
    return true;
}


static bool
TestWatchAll(const char* testDir)
{
    printf("\n=== Test: B_WATCH_ALL ===\n");
    
    /* Create test directory and file */
    create_directory(testDir, 0755);
    BString filePath(testDir);
    filePath << "/watch_all.txt";
    
    BFile file(filePath.String(), B_CREATE_FILE | B_WRITE_ONLY);
    file.Write("Test", 4);
    file.Unset();
    
    /* Set up watcher with all flags */
    TestLooper* looper = new TestLooper();
    TestHandler* handler = new TestHandler("WatchAll");
    looper->AddHandler(handler);
    looper->Run();
    
    BNode node(filePath.String());
    node_ref nref;
    node.GetNodeRef(&nref);
    
    status_t err = watch_node(&nref, B_WATCH_ALL, handler);
    if (err != B_OK) {
        printf("watch_node failed: %s\n", strerror(err));
        looper->Lock();
        looper->Quit();
        return false;
    }
    printf("Watching with B_WATCH_ALL: %s\n", filePath.String());
    
    /* Trigger stat change */
    printf("Modifying file...\n");
    file.SetTo(filePath.String(), B_WRITE_ONLY);
    file.Write("Changed", 7);
    file.Unset();
    
    WaitForEvents(1, handler);
    
    bool statOk = (handler->EventCount() > 0);
    printf("  Received events: %d - %s\n", handler->EventCount(), 
           statOk ? "PASS" : "FAIL");
    
    /* Cleanup */
    stop_watching(handler);
    looper->Lock();
    looper->Quit();
    unlink(filePath.String());
    rmdir(testDir);
    
    return statOk;
}


/* ============================================================================
 * Main
 * ============================================================================
 */

int main(int argc, char** argv)
{
    printf("====================================\n");
    printf("Haiku Node Monitor API Test\n");
    printf("====================================\n");
    
    /* Need a BApplication for messaging */
    BApplication app("application/x-vnd.Test-NodeMonitor");
    
    const char* testDir = "/tmp/haiku_nm_test";
    
    int passed = 0;
    int failed = 0;
    
    /* Run tests */
    if (TestDirectoryWatch(testDir)) passed++; else failed++;
    if (TestStatWatch(testDir)) passed++; else failed++;
    if (TestNameWatch(testDir)) passed++; else failed++;
    if (TestWatchAll(testDir)) passed++; else failed++;
    if (TestVolumeRoster()) passed++; else failed++;
    if (TestMountWatch()) passed++; else failed++;
    
    printf("\n====================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("====================================\n");
    
    return failed > 0 ? 1 : 0;
}
