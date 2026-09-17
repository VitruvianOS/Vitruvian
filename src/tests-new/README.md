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
- Use `CHECK` when possible so that one failure doesn't hide any others.
- Use `REQUIRE` when continuing would be meaningless or crash.

### Recording a known bug

A test that is known broken tagged `[!shouldfail]`, so Catch2 reports it as
"failed as expected" and the run passes. Once the bug is fixed the test passes
which is reported as a failure so the test can be flipped.

ctest only sees whether the executable failed, so put `KNOWN BUG: ` at the start
of the test case name so it's easy to find.

```cpp
TEST_CASE("KNOWN BUG: wait_for_thread does not wait for the thread to finish",
	"[thread][libroot][known-bug][!shouldfail]")
```

Also add the `[known-bug]` tag, so that `<executable> --list-tests "[known-bug]"`
lists them.

## Utilities

Shared test helpers live in `utils/`, which `UnitTest()` puts on the include
path.

- `StringMakers.h`: Catch2 printers for various types
- `ThreadedTest.h`: runs several threads in one test case. Catch2's assertion
  macros are not thread safe, so worker threads use `THREAD_CHECK` and
  `THREAD_REQUIRE`

  ```cpp
  ThreadedTest test;
  test.AddThread("A", [&]() { THREAD_REQUIRE(locker.Lock()); ... });
  test.AddThread("B", [&]() { ... });
  test.Run();
  ```

- `ScopedMemoryLimit.h`: temporarily caps how much more memory the process
  may use for testing overallocation. Linux overcommits memory, so without this
  the allocation can succeed and the test gets OOM killed instead.

  ```cpp
  BString string("Base");
  {
  	ScopedMemoryLimit limit;
  	string.Append('C', 2000000000);
  }
  CHECK(string == "Base");
  ```
