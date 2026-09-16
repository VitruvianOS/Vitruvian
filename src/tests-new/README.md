# Vitruvian Testing

This directory holds tests for the Vitruvian API and internals as needed. Most
tests are [Catch 2](https://catch2.org) based unit tests. There are also manual
tests, but their use is discouraged except in cases where absolutley necessary
such as appearance tests for UI elements.

## Directory Layout

The layout mostly mirrors `src/`:

```
tests-new/
├── add-ons/
├── apps/                 # Tests for the built in applications
├── kits/
│   ├── support/          # One directory per kit
│   └── .../
├── libs/
├── preferences/
├── servers/
├── system/
└── tools/                # Assorted test tools
└── utils/                # Common utilities used by some tests
```

- Each kit (or server, library, etc.) builds one test executable named
  `<name>-tests`, e.g. `support-kit-tests`.
- Each class under test gets one source file named after the class without
  its `B` prefix, e.g. `MemoryIOTest.cpp` for `BMemoryIO`. No headers are
  needed.
- A helper class shared by several test files (e.g. an archivable object to
  instantiate) does get a header, named `<Kit><Thing>.h`, e.g.
  `ArchivableTestObject.h`.
- A large class may be split into one file per group of related methods,
  named `<Class><Area>Test.cpp`, e.g. `StringSearchTest.cpp` and
  `StringReplaceTest.cpp` for `BString`.

## Running the Tests

Assuming you have a working build setup, you should be able to run:
```bash
cd generated.amd64  # Or whatever your generated.<platform> is
ctest               # add -j to run it across all cores, speeding it up considerably 
```

Every Catch2 test case is registered with CTest individually, so you can
select tests by name:
```bash
ctest -R BMemoryIO --output-on-failure
```

Or run a test executable directly and filter by tag:
```bash
src/tests-new/kits/support/support-kit-tests "[BMallocIO]"
```

Run an executable with `-?` to see all of Catch2's options.

## Adding New Tests

### Build

Use the `UnitTest()` function defined in `CMakeLists.txt`. It links libbe,
libroot and Catch2's `main`, applies the build compatibility header that other
Vitruvian targets get, and registers the test cases with CTest:

```cmake
UnitTest(support-kit-tests
	SOURCES
	MallocIOTest.cpp
	MemoryIOTest.cpp

	LIBS             # optional, extra libraries
	shared
)
```

When adding a new directory, remember to `add_subdirectory()` it from its
parent.

### Writing tests

```cpp
#include <DataIO.h>

#include <catch2/catch_test_macros.hpp>


TEST_CASE("BMemoryIO: Read", "[BMemoryIO][support]")
{
	char buf[20] = "0123456789ABCDEFGHI";
	BMemoryIO mem(buf, 20);

	SECTION("ReadAt() past the end reads nothing")
	{
		char readBuf[10];
		CHECK(mem.ReadAt(30, readBuf, 10) == 0);
	}
}
```

- Name test cases `"<Class>: <what is tested>"`.
- Tag every test case with the class and the kit, e.g. `[BMemoryIO][support]`.
- Prefer `CHECK` so one failure does not hide the rest. Use `REQUIRE` only
  when continuing would be meaningless or crash (e.g. a failed allocation).
- Write comparisons as plain expressions (`CHECK(a == b)`), not
  `CHECK(strcmp(...) == 0)` so that Catch2 can print both values on failure. For C
  strings compare `std::string` values.
- Catch2 re-runs the whole test case from the top for each `SECTION`. Use
  sections for steps that are independent of each other. When each step
  depends on the state left by the previous one (a stream position, a buffer
  size), write the checks in sequence without sections.

### Recording a known bug

A test for behaviour that is currently broken is tagged `[!shouldfail]`, so
Catch2 reports it as "failed as expected" and the run passes. Once the
bug is fixed the test passes, which Catch2 then reports as a failure, and
whoever fixed it removes the tag.

ctest only sees whether the executable failed, so such a test otherwise shows
up as an ordinary pass. Put `KNOWN BUG: ` at the start of the test case name,
and state what is broken, so it is visible in ctest output:

```cpp
TEST_CASE("KNOWN BUG: wait_for_thread does not wait for the thread to finish",
	"[thread][libroot][known-bug][!shouldfail]")
```

Also add the `[known-bug]` tag, so that `<executable> --list-tests "[known-bug]"`
lists them.

### Porting cppunit tests from `src/tests`

| cppunit                                | Catch2                                  |
| -------------------------------------- | --------------------------------------- |
| `BTestCase` subclass + `PerformTest()` | `TEST_CASE(...)`                        |
| `suite()` / `*TestSuite()` / add-on    | nothing, CTest discovers test cases     |
| `NextSubTest()`                        | `SECTION` or sequential checks (above)  |
| `CPPUNIT_ASSERT(expr)`, `CHK(expr)`    | `CHECK(expr)`                           |
| `#ifndef TEST_R5` / `TEST_OBOS`        | remove, keep the non-R5 code            |

Once a port builds and passes, remove the old test from `src/tests`: its
sources from the add-on's `CMakeLists.txt`, its registration in the
`*TestAddon.cpp`, its `run_cppunit` line in `runsuite.sh`, and the files
themselves.

## Utilities

Shared test helpers live in `utils/`, which `UnitTest()` puts on the include
path.

- `StringMakers.h`: Catch2 printers for Vitruvian types so a failed `CHECK(a == b)` shows both values instead of `{?}`.
  Include it after `<catch2/catch_test_macros.hpp>`. Add a printer here when a new type shows up in checks.
- `ThreadedTest.h`: runs several threads in one test case. Catch2's assertion
  macros are not thread safe, so worker threads use `THREAD_CHECK` and
  `THREAD_REQUIRE`, which record failures; `Run()` reports them on the main
  thread once every thread has finished. `THREAD_REQUIRE` ends its own thread,
  like `CPPUNIT_ASSERT` did, so a failing loop reports once instead of
  thousands of times.

  ```cpp
  ThreadedTest test;
  test.AddThread("A", [&]() { THREAD_REQUIRE(locker.Lock()); ... });
  test.AddThread("B", [&]() { ... });
  test.Run();
  ```

  Note it joins threads with a semaphore rather than `wait_for_thread()`,
  which currently returns immediately instead of waiting. Its own tests are in
  `ThreadedTestTest.cpp`, built as `utils-tests`.
- `ScopedMemoryLimit.h`: temporarily caps how much more memory the process
  may use. Use it when a test expects a huge allocation to fail: Linux
  overcommits memory, so without a limit the allocation can succeed and the
  test gets killed instead.

  ```cpp
  BString string("Base");
  {
  	ScopedMemoryLimit limit;
  	string.Append('C', 2000000000);
  }
  CHECK(string == "Base");
  ```
