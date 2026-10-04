# SPDX-License-Identifier: GPL-3.0-or-later
# Drawpile Mobile (fork): sources of the mobile interface. Included from
# src/desktop/CMakeLists.txt so the fork only adds one line there.

target_sources(drawpile PRIVATE
	mobile/chrome.cpp
	mobile/chrome.h
	mobile/commandsheet.cpp
	mobile/commandsheet.h
	mobile/hub.cpp
	mobile/hub.h
	mobile/inventory.cpp
	mobile/mobileui.cpp
	mobile/mobileui.h
	mobile/mobileui.qrc
	mobile/panels.cpp
	mobile/panels.h
	mobile/sheet.cpp
	mobile/sheet.h
	mobile/shell.cpp
	mobile/shell.h
	mobile/theme.cpp
	mobile/theme.h
	mobile/thumbnails.cpp
	mobile/thumbnails.h
	mobile/widgets.cpp
	mobile/widgets.h
)

# zlib for reading OpenRaster thumbnails, SQLite for project thumbnails. Both
# are already dependencies of Drawdance.
find_package(ZLIB REQUIRED)
target_link_libraries(drawpile PRIVATE ZLIB::ZLIB dpdb)
