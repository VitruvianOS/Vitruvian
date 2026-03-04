#!/bin/sh
# runsuite.sh -- Vitruvian Test Suite Runner
# Runs all automated tests and produces per-test log files
# Usage: ./runsuite.sh [--system] [--beapi]
#   --system : Run system tests only (nexus + libroot, no libbe)
#   --beapi  : Run BeAPI tests only (requires full runtime with libbe)
#   (default): Run both system and BeAPI tests

LOGDIR="/tmp/vitruvian-test-logs"
UNITTESTER="/system/tests/UnitTester"
TESTBIN="/system/tests"
TIMEOUT_CMD="timeout 120"

# Ensure libcppunit.so is findable by the dynamic linker
export LD_LIBRARY_PATH="/system/tests:${LD_LIBRARY_PATH}"

mkdir -p "$LOGDIR"

TOTAL=0
PASS=0
FAIL=0
ERRORS=""

run_test() {
    local name="$1"
    local cmd="$2"
    local logfile="$LOGDIR/${name}.log"

    TOTAL=$((TOTAL + 1))
    printf "  %-45s" "$name"
    $TIMEOUT_CMD sh -c "$cmd" > "$logfile" 2>&1
    local rc=$?
    if [ $rc -eq 124 ]; then
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  TIMEOUT: $name (see $logfile)"
        echo "TIMEOUT"
    elif [ $rc -ne 0 ]; then
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  FAIL: $name (exit $rc, see $logfile)"
        echo "FAIL (exit $rc)"
    else
        if grep -q "^- FAIL" "$logfile" 2>/dev/null; then
            FAIL=$((FAIL + 1))
            ERRORS="$ERRORS\n  FAIL: $name (see $logfile)"
            echo "FAIL"
        else
            PASS=$((PASS + 1))
            echo "PASS"
        fi
    fi
}

run_cppunit() {
    local suite="$1"
    local logname="$2"
    run_test "$logname" "$UNITTESTER $suite"
}

RUN_SYSTEM=1
RUN_BEAPI=1

for arg in "$@"; do
    case "$arg" in
        --system)
            RUN_BEAPI=0
            ;;
        --beapi)
            RUN_SYSTEM=0
            ;;
    esac
done

echo "=========================================="
echo " Vitruvian Test Suite"
echo "=========================================="
echo "Log directory: $LOGDIR"
echo ""

# ============================================================
# SYSTEM TESTS (nexus + libroot tests)
# ============================================================
if [ "$RUN_SYSTEM" -eq 1 ]; then
    echo "------------------------------------------"
    echo " System Tests (no libbe required)"
    echo "------------------------------------------"

    echo ""
    echo "  [VOS Testharness]"
    run_test "vos-testlist"           "$TESTBIN/testlist"
    run_test "vos-testoskit"          "$TESTBIN/testoskit"
    run_test "vos-testports"          "$TESTBIN/testports"
    run_test "vos-testports2"         "$TESTBIN/testports2"
    run_test "vos-testsem"            "$TESTBIN/testsem"
    run_test "vos-testsem2"           "$TESTBIN/testsem2"
    run_test "vos-testsemdeletion"    "$TESTBIN/testsemdeletion"
    run_test "vos-testthread"         "$TESTBIN/testthread"
    run_test "vos-testteam"           "$TESTBIN/testteam"
    run_test "vos-testfsinfo"         "$TESTBIN/testfsinfo"
    run_test "vos-test_team_send_data" "$TESTBIN/test_team_send_data"
    run_test "vos-set_port_owner"     "$TESTBIN/set_port_owner"
    #run_test "vos-test_load_image"    "$TESTBIN/test_load_image"
    #run_test "vos-test_load_image2"   "$TESTBIN/test_load_image2"
    run_test "vos-test_area"          "$TESTBIN/test_area"
    run_test "vos-testvref"           "$TESTBIN/testvref"
    run_test "vos-teststopwatch"      "$TESTBIN/teststopwatch"

    #echo ""
    #echo "  [VOS Port Tests]"
    #run_test "vos-port-writer"        "$TESTBIN/writer"
    #run_test "vos-port-reader"        "$TESTBIN/reader"
    #run_test "vos-port-order"         "$TESTBIN/order"

    echo ""
    echo "  [System Kernel Tests]"
    run_test "kernel-fibo_load_image"      "$TESTBIN/fibo_load_image 20"
    run_test "kernel-path_resolution_test" "$TESTBIN/path_resolution_test"
    run_test "kernel-port_close_test_1"    "$TESTBIN/port_close_test_1"
    run_test "kernel-port_close_test_2"    "$TESTBIN/port_close_test_2"
    run_test "kernel-port_delete_test"     "$TESTBIN/port_delete_test"
    run_test "kernel-port_multi_read_test" "$TESTBIN/port_multi_read_test"
    run_test "kernel-port_wakeup_test_1"   "$TESTBIN/port_wakeup_test_1"
    run_test "kernel-port_wakeup_test_2"   "$TESTBIN/port_wakeup_test_2"
    run_test "kernel-port_wakeup_test_3"   "$TESTBIN/port_wakeup_test_3"
    run_test "kernel-port_wakeup_test_4"   "$TESTBIN/port_wakeup_test_4"
    run_test "kernel-port_wakeup_test_5"   "$TESTBIN/port_wakeup_test_5"
    run_test "kernel-port_wakeup_test_6"   "$TESTBIN/port_wakeup_test_6"
    run_test "kernel-port_wakeup_test_7"   "$TESTBIN/port_wakeup_test_7"
    run_test "kernel-port_wakeup_test_8"   "$TESTBIN/port_wakeup_test_8"
    run_test "kernel-port_wakeup_test_9"   "$TESTBIN/port_wakeup_test_9"
    #run_test "kernel-sem_acquire_test1"    "$TESTBIN/sem_acquire_test1"
    #run_test "kernel-wait_for_objects_test" "$TESTBIN/wait_for_objects_test"

    echo ""
    echo "  [System Libroot Tests]"
    #run_test "libroot-system_watching"     "$TESTBIN/system_watching_test"

    echo ""
    echo "  [Support Kit System Tests]"
    #run_test "support-compression"         "$TESTBIN/compression_test"  # utility, needs input/output files
    run_test "support-string_utf8"         "$TESTBIN/string_utf8_tests"

    echo ""
    echo "  [App Kit System Tests]"
    run_test "app-portlink"                "$TESTBIN/PortLinkTest"
fi

# ============================================================
# BEAPI TESTS (requires libbe and servers)
# ============================================================
if [ "$RUN_BEAPI" -eq 1 ]; then
    echo ""
    echo "------------------------------------------"
    echo " BeAPI Tests (libbe + full runtime)"
    echo "------------------------------------------"

    echo ""
    echo "  [Support Kit CppUnit]"
    run_cppunit "BArchivable"   "support-BArchivable"
    run_cppunit "BAutolock"     "support-BAutolock"
    run_cppunit "BDateTime"     "support-BDateTime"
    run_cppunit "BLocker"       "support-BLocker"
    run_cppunit "BMemoryIO"     "support-BMemoryIO"
    run_cppunit "BMallocIO"     "support-BMallocIO"
    run_cppunit "BString"       "support-BString"
    run_cppunit "BBlockCache"   "support-BBlockCache"
    run_cppunit "ByteOrder"     "support-ByteOrder"

    echo ""
    echo "  [Storage Kit CppUnit]"
    run_cppunit "BAppFileInfo"  "storage-BAppFileInfo"
    run_cppunit "BDirectory"    "storage-BDirectory"
    run_cppunit "BEntry"        "storage-BEntry"
    run_cppunit "BFile"         "storage-BFile"
    run_cppunit "BNode"         "storage-BNode"
    run_cppunit "BNodeInfo"     "storage-BNodeInfo"
    run_cppunit "BPath"         "storage-BPath"
    run_cppunit "BResources"    "storage-BResources"
    run_cppunit "BResourceStrings" "storage-BResourceStrings"
    run_cppunit "BSymLink"      "storage-BSymLink"
    #run_cppunit "FindDirectory" "storage-FindDirectory"  # needs mkbfs/unmount Haiku-specific tools
    run_cppunit "MimeSniffer"   "storage-MimeSniffer"

    echo ""
    echo "  [App Kit CppUnit]"
    run_cppunit "BApplication"    "app-BApplication"
    run_cppunit "BClipboard"      "app-BClipboard"
    run_cppunit "BHandler"        "app-BHandler"
    run_cppunit "BLooper"         "app-BLooper"
    run_cppunit "BMessageQueue"   "app-BMessageQueue"
    run_cppunit "BMessageRunner"  "app-BMessageRunner"
    run_cppunit "BMessenger"      "app-BMessenger"
    run_cppunit "BPropertyInfo"   "app-BPropertyInfo"
    run_cppunit "BRoster"         "app-BRoster"

    echo ""
    echo "  [Interface Kit CppUnit]"
    run_cppunit "BPolygon"        "interface-BPolygon"
    run_cppunit "BRegion"         "interface-BRegion"
    run_cppunit "GraphicsDefs"    "interface-GraphicsDefs"
    run_cppunit "BAlert"          "interface-BAlert"
    run_cppunit "BBitmap"         "interface-BBitmap"
    run_cppunit "BDeskbar"        "interface-BDeskbar"
    run_cppunit "BMenu"           "interface-BMenu"
    run_cppunit "BOutlineListView" "interface-BOutlineListView"
    run_cppunit "BTextControl"    "interface-BTextControl"
    run_cppunit "BTextView"       "interface-BTextView"

    echo ""
    echo "  [Shared Kit CppUnit]"
    run_cppunit "CalendarViewTest"              "shared-CalendarView"
    run_cppunit "NaturalCompareTest"            "shared-NaturalCompare"
    run_cppunit "JsonEndToEndTest"              "shared-JsonEndToEnd"
    run_cppunit "JsonErrorHandlingTest"         "shared-JsonErrorHandling"
    run_cppunit "JsonTextWriterTest"            "shared-JsonTextWriter"
    run_cppunit "JsonToMessageTest"             "shared-JsonToMessage"
    run_cppunit "KeymapTest"                    "shared-Keymap"

    echo ""
    echo "  [App Kit Runtime Tests]"
    run_test "app-handler_looper_msg"  "$TESTBIN/HandlerLooperMessageTest"

    echo ""
    echo "  [VOS Runtime Tests]"
    #run_test "vos-test_node_monitor"   "$TESTBIN/test_node_monitor"   # needs /dev/haiku_node_monitor kernel module
    #run_test "vos-test_node_monitor2"  "$TESTBIN/test_node_monitor2"  # needs kernel module

    echo ""
    echo "  [Storage Kit Runtime Tests]"
    #run_test "storage-NodeMonitorTest"   "$TESTBIN/NodeMonitorTest"    # needs kernel node monitor module
    run_test "storage-PathMonitorTest"   "$TESTBIN/PathMonitorTest"
    run_test "storage-PathMonitorTest2"  "rm -rf /tmp/path-monitor-test; $TESTBIN/PathMonitorTest2"

    echo ""
    echo "  [Registrar Tests]"
    run_test "server-RegistrarTest1"   "$TESTBIN/RegistrarTest1"
fi

echo ""
echo "=========================================="
echo " Test Suite Summary"
echo "=========================================="
echo "Total: $TOTAL  Passed: $PASS  Failed: $FAIL"
if [ -n "$ERRORS" ]; then
    echo ""
    echo "Failed tests:"
    printf "$ERRORS\n"
fi
echo "=========================================="
echo "Logs in: $LOGDIR"
