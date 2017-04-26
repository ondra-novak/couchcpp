cmake_minimum_required(VERSION 3.0)
add_compile_options(-std=c++11)
add_executable (couchcpp couchcpp.cpp module.cpp) 
target_link_libraries (couchcpp LINK_PUBLIC imtjson dl -rdynamic)

file(GLOB couchcpp_HDR "parts/*.h")

INSTALL(TARGETS couchcpp
        DESTINATION "bin"
        ) 
INSTALL(FILES couchcpp.ini
        DESTINATION "/etc/couchdb/default.d"
        ) 
INSTALL(FILES couchcpp.conf
        DESTINATION "/etc/couchdb"
        ) 
        
INSTALL(FILES ${couchcpp_HDR}
        DESTINATION "include/couchcpp/parts"
        )      
INSTALL(FILES api.h
        DESTINATION "include/couchcpp"
        )      
        
INSTALL (CODE "execute_process(COMMAND mkdir -p /var/cache/couchcpp)")
INSTALL (CODE "execute_process(COMMAND chown couchdb:couchdb /var/cache/couchcpp)")
INSTALL (CODE "execute_process(COMMAND service couchdb restart)")
