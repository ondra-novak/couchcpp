cmake_minimum_required(VERSION 2.8)
add_compile_options(-std=c++11)
add_executable (couchcpp couchcpp.cpp assembly.cpp) 
target_link_libraries (couchcpp LINK_PUBLIC imtjson dl)

install(TARGETS couchcpp
        DESTINATION "bin"
        ) 
INSTALL(FILES couchcpp.ini
        DESTINATION "/etc/couchdb/default.d"
        ) 
INSTALL(FILES couchcpp.conf
        DESTINATION "/etc/couchcpp"
        ) 
        
install (SCRIPT "${CMAKE_SOURCE_DIR}/PostInstall.cmake")
