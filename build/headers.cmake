set(
	PUBLIC_HEADERS

	"headers/"
	"headers/os"
	"headers/os/app"
	"headers/os/drivers"
	"headers/os/kernel"
	"headers/os/support"
	"headers/os/storage"
	"headers/os/translation"

	"headers/os/config"
	"headers/os/add-ons/file_system"
	"headers/os/add-ons/graphics"
	"headers/os/add-ons/registrar"
	"headers/os/app"
	"headers/os/arch/x86_64"
	"headers/os/device"
	"headers/os/drivers/bus"
	"headers/os/game"
	"headers/os/interface"
	"headers/os/kernel"
	"headers/os/locale/"
	"headers/os/locale/tools"
	"headers/os/mail"
	"headers/os/net"
	"headers/os/storage"
	"headers/os/support"
	"headers/os/posix/compat/sys"

	"headers/os/add-ons/input_server"

	"headers/posix"
)

set(
	PRIVATE_HEADERS

	"headers/private/"

	"headers/private/input"
	"headers/private/shared"
	"headers/private/storage"
	"headers/private/system/arch/x86/"
	"headers/private/kernel/arch/x86/"

	"headers/libs/agg/"
	"headers/libs/linprog/"

	"src/kits/tracker/"
	"src/system/libroot/os/"
)

set(
	BUILD_HEADERS
	"headers/build"
	"headers/build/config_headers"
	"headers/config"
)

set(
	BUILDTOOLS_MODE_HEADERS
	"headers/build"
	"headers/build/config_headers"
	"headers/config"

	"headers/build/os"
	"headers/build/os/app"
	"headers/build/os/drivers"
	"headers/build/os/interface"
	"headers/build/os/kernel"
	"headers/build/os/support"
	"headers/build/os/storage"
	"headers/build/os/storage/mime"
	"headers/build/os/storage/sniffer"
	"headers/build/os/add-ons/registrar/"
	"headers/private/system/arch/x86_64/"
	"headers/private/kernel/arch/"
	"headers/private/kernel/arch/x86/"
)

if(NOT BUILDTOOLS_MODE STREQUAL "1")
# Standard include directories
include_directories(
	AFTER
	"headers"
	${BUILD_HEADERS}
	${PRIVATE_HEADERS}
	${PUBLIC_HEADERS}
)
else()
include_directories(
	AFTER
	"headers"
	${BUILDTOOLS_MODE_HEADERS}
)
endif()
