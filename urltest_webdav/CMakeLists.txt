CMAKE_MINIMUM_REQUIRED (VERSION 2.6)
PROJECT (urltest_webdav C)

ADD_EXECUTABLE(urltest_webdav-exe urltest_webdav.c)
SET_TARGET_PROPERTIES(urltest_webdav-exe PROPERTIES OUTPUT_NAME urltest_webdav)
TARGET_LINK_LIBRARIES(urltest_webdav-exe urltest -lm ${CURL_LIBRARIES})
INCLUDE_DIRECTORIES(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/../lib ${CMAKE_SOURCE_DIR}/lib)
INSTALL(TARGETS urltest_webdav-exe DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT binaries)

