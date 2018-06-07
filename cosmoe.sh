# check for the common mistake of not being root
# this will change one day, but for now root is required
#if [ `whoami` != "root" ]
#then
#    echo "ERROR: You must execute this script as root."
#    echo "       Read the README file for more information."
#    exit 1
#fi

# remove stale shared memory segments
clean_shm.sh

# start appserver, registrar and a demo app
appserver > server.out &
sleep 2

registrar > registrar.out &
sleep 1

guido > guido.out
sleep 1

# if we arrived here, the application has terminated
killall guido
killall registrar
killall appserver
sleep 1

# if something won't die normally, try harder to kill it
killall -9 guido
killall -9 registrar
killall -9 appserver

# remove stale shared memory segments
clean_shm.sh
