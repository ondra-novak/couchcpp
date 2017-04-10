execute_process(COMMAND mkdir -p /var/lib/couchcpp)
execute_process(COMMAND chmod 0777 /var/lib/couchcpp)
execute_process(COMMAND /usr/sbin/service couchdb restart)

